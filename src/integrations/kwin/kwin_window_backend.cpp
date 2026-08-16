// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// kwin_window_backend.cpp
//
// Implementation overview:
// Implements the in-process KWin state model, snapshot transaction,
// revision ordering, capability reporting, and command dispatch.
//
// Important implementation decisions:
// - Complete snapshots are staged and committed atomically.
// - Incremental messages must advance the accepted revision.
// - Current-desktop state is applied consistently to every window.
// - Backend notifications occur only after authoritative state changes.
// - Action calls contain no D-Bus code and use injected handlers.
//
// ------------------------------------------------------------

#include "kwin_window_backend.h"

#include "kwin_integration_protocol.h"

#include <algorithm>
#include <utility>

void KWinWindowBackend::start()
{
    m_started = true;
}

void KWinWindowBackend::stop()
{
    if (!m_started)
        return;

    unregister_integration();
    m_started = false;
}

std::string KWinWindowBackend::name() const
{
    return "KWin";
}

WindowBackendCapabilities
KWinWindowBackend::capabilities() const
{
    WindowBackendCapabilities capabilities;

    capabilities.can_activate = true;
    capabilities.can_raise = true;
    capabilities.can_close = true;
    capabilities.can_minimize = true;
    capabilities.can_maximize = true;
    capabilities.provides_stacking_order =
        true;
    capabilities.provides_activities = true;
    capabilities.provides_virtual_desktops =
        true;
    capabilities.provides_frame_geometry =
        true;
    capabilities.provides_icons = true;
    capabilities.accepts_icon_geometry =
        true;

    return capabilities;
}

bool KWinWindowBackend::connected() const
{
    return m_connected;
}

std::vector<ManagedWindow>
KWinWindowBackend::windows() const
{
    return m_windows;
}

std::vector<WindowId>
KWinWindowBackend::stacking_order() const
{
    return m_stacking_order;
}

std::optional<WindowId>
KWinWindowBackend::active_window() const
{
    return m_active_window;
}

std::optional<WindowIconGeometry>
KWinWindowBackend::
    dock_surface_geometry() const
{
    return m_dock_surface_geometry;
}

bool KWinWindowBackend::activate_window(
    const WindowId &window_id)
{
    return dispatch_command(
        KWinWindowCommandType::ACTIVATE,
        window_id);
}

bool KWinWindowBackend::present_windows(
    const std::vector<WindowId>
        &window_ids)
{
    if (!m_connected ||
        !m_command_handler ||
        window_ids.empty() ||
        std::any_of(
            window_ids.begin(),
            window_ids.end(),
            [this](
                const WindowId &window_id)
            {
                return !find_window(
                    window_id);
            }))
    {
        return false;
    }

    if (m_registered_protocol_version <
        KWinIntegrationProtocol::VERSION)
    {
        bool accepted = true;

        for (const auto &window_id :
             window_ids)
        {
            const auto window =
                find_window(window_id);

            if (window->minimized)
            {
                accepted =
                    m_command_handler(
                        KWinWindowCommand{
                            window_id,
                            KWinWindowCommandType::
                                SET_MINIMIZED,
                            false,
                            {}}) &&
                    accepted;
            }

            accepted =
                m_command_handler(
                    KWinWindowCommand{
                        window_id,
                        KWinWindowCommandType::RAISE,
                        false,
                        {}}) &&
                accepted;
        }

        accepted =
            m_command_handler(
                KWinWindowCommand{
                    window_ids.back(),
                    KWinWindowCommandType::ACTIVATE,
                    false,
                    {}}) &&
            accepted;

        return accepted;
    }

    return m_command_handler(
        KWinWindowCommand{
            window_ids.back(),
            KWinWindowCommandType::PRESENT,
            false,
            window_ids});
}

bool KWinWindowBackend::hide_windows(
    const std::vector<WindowId>
        &window_ids)
{
    if (!m_connected ||
        !m_command_handler ||
        window_ids.empty() ||
        std::any_of(
            window_ids.begin(),
            window_ids.end(),
            [this](
                const WindowId &window_id)
            {
                return !find_window(
                    window_id);
            }))
    {
        return false;
    }

    if (m_registered_protocol_version <
        KWinIntegrationProtocol::VERSION)
    {
        bool accepted = true;

        for (const auto &window_id :
             window_ids)
        {
            accepted =
                m_command_handler(
                    KWinWindowCommand{
                        window_id,
                        KWinWindowCommandType::
                            SET_MINIMIZED,
                        true,
                        {}}) &&
                accepted;
        }

        return accepted;
    }

    return m_command_handler(
        KWinWindowCommand{
            window_ids.back(),
            KWinWindowCommandType::HIDE,
            true,
            window_ids});
}

bool KWinWindowBackend::raise_window(
    const WindowId &window_id)
{
    return dispatch_command(
        KWinWindowCommandType::RAISE,
        window_id);
}

bool KWinWindowBackend::close_window(
    const WindowId &window_id)
{
    return dispatch_command(
        KWinWindowCommandType::CLOSE,
        window_id);
}

bool KWinWindowBackend::
    set_window_minimized(
        const WindowId &window_id,
        bool minimized)
{
    return dispatch_command(
        KWinWindowCommandType::
            SET_MINIMIZED,
        window_id,
        minimized);
}

bool KWinWindowBackend::
    set_window_maximized(
        const WindowId &window_id,
        bool maximized)
{
    return dispatch_command(
        KWinWindowCommandType::
            SET_MAXIMIZED,
        window_id,
        maximized);
}

bool KWinWindowBackend::
    set_window_icon_geometry(
        const WindowId &window_id,
        const WindowIconGeometry &geometry)
{
    return !window_id.empty() &&
           geometry.width > 0 &&
           geometry.height > 0 &&
           m_icon_geometry_handler &&
           m_icon_geometry_handler(
               window_id,
               geometry);
}

void KWinWindowBackend::
    set_command_handler(
        KWinWindowCommandHandler handler)
{
    m_command_handler =
        std::move(handler);
}

void KWinWindowBackend::
    set_icon_geometry_handler(
        IconGeometryHandler handler)
{
    m_icon_geometry_handler =
        std::move(handler);
}

bool KWinWindowBackend::register_integration(
    std::uint32_t protocol_version)
{
    if (!m_started ||
        (protocol_version !=
             KWinIntegrationProtocol::VERSION &&
         protocol_version !=
             KWinIntegrationProtocol::
                 LEGACY_VERSION))
    {
        return false;
    }

    if (!m_connected)
    {
        clear_state();
        m_registered = true;
        m_registered_protocol_version =
            protocol_version;
        return true;
    }

    // A script can re-register after one incremental update was rejected.
    // Keep the last complete state visible until its replacement snapshot
    // commits. Clearing here creates a false "application is closed" gap:
    // clicking the dock item during that gap launches a duplicate window.
    m_staged_windows.clear();
    m_staged_revision.reset();
    m_last_revision = 0;
    m_registered = true;
    m_registered_protocol_version =
        protocol_version;

    return true;
}

void KWinWindowBackend::
    unregister_integration()
{
    const bool was_connected =
        m_connected;

    clear_state();

    if (was_connected)
        notify_connection_changed(false);
}

bool KWinWindowBackend::begin_snapshot(
    std::uint64_t revision)
{
    if (!m_started ||
        !m_registered ||
        revision <= m_last_revision ||
        (m_staged_revision &&
         revision <= *m_staged_revision))
    {
        return false;
    }

    m_staged_windows.clear();
    m_staged_revision = revision;

    return true;
}

bool KWinWindowBackend::stage_window(
    std::uint64_t revision,
    const ManagedWindow &window)
{
    if (!m_staged_revision ||
        *m_staged_revision != revision ||
        window.id.empty())
    {
        return false;
    }

    const auto staged_window =
        std::find_if(
            m_staged_windows.begin(),
            m_staged_windows.end(),
            [&window](
                const ManagedWindow
                    &candidate)
            {
                return candidate.id ==
                       window.id;
            });

    if (staged_window ==
        m_staged_windows.end())
    {
        m_staged_windows.push_back(
            window);
    }
    else
    {
        *staged_window = window;
    }

    return true;
}

// Atomically replaces the visible backend state with a fully staged
// revision. Consumers are notified only after the snapshot is complete, so
// they never observe a partially synchronized KWin window list.
bool KWinWindowBackend::commit_snapshot(
    std::uint64_t revision,
    const std::optional<WindowId>
        &active_window,
    const std::vector<WindowId>
        &stacking_order)
{
    if (!m_staged_revision ||
        *m_staged_revision != revision)
    {
        return false;
    }

    const bool was_connected =
        m_connected;

    m_windows =
        std::move(m_staged_windows);
    m_staged_windows.clear();
    m_staged_revision.reset();

    m_stacking_order = stacking_order;
    m_active_window.reset();

    if (active_window &&
        find_window(*active_window))
    {
        m_active_window = active_window;
    }

    for (auto &window :
         m_windows)
    {
        apply_current_desktop(window);
        window.active =
            m_active_window &&
            window.id ==
                *m_active_window;
    }

    m_last_revision = revision;
    m_connected = true;

    if (was_connected)
        notify_snapshot_changed();
    else
        notify_connection_changed(true);

    return true;
}

void KWinWindowBackend::cancel_snapshot()
{
    m_staged_windows.clear();
    m_staged_revision.reset();
}

bool KWinWindowBackend::publish_window(
    std::uint64_t revision,
    const ManagedWindow &window)
{
    if (!accepts_incremental_revision(
            revision) ||
        window.id.empty())
    {
        return false;
    }

    auto current =
        find_window(window.id);

    const bool added = !current;

    if (added)
    {
        m_windows.push_back(window);
        current = &m_windows.back();
    }
    else
    {
        *current = window;
    }

    current->active =
        m_active_window &&
        current->id == *m_active_window;
    apply_current_desktop(*current);

    m_last_revision = revision;

    if (added)
        notify_window_added(*current);
    else
        notify_window_updated(*current);

    return true;
}

bool KWinWindowBackend::
    publish_current_desktop(
        std::uint64_t revision,
        const std::string &desktop_id,
        unsigned int desktop_number)
{
    if (!accepts_incremental_revision(
            revision) ||
        (desktop_id.empty() &&
         desktop_number == 0))
    {
        return false;
    }

    m_current_desktop_id = desktop_id;
    m_current_desktop_number =
        desktop_number;

    for (auto &window : m_windows)
        apply_current_desktop(window);

    m_last_revision = revision;
    notify_snapshot_changed();
    return true;
}

bool KWinWindowBackend::
    publish_window_removed(
        std::uint64_t revision,
        const WindowId &window_id)
{
    if (!accepts_incremental_revision(
            revision) ||
        window_id.empty())
    {
        return false;
    }

    const auto previous_size =
        m_windows.size();

    m_windows.erase(
        std::remove_if(
            m_windows.begin(),
            m_windows.end(),
            [&window_id](
                const ManagedWindow
                    &window)
            {
                return window.id ==
                       window_id;
            }),
        m_windows.end());

    if (m_windows.size() == previous_size)
        return false;

    const bool active_window_removed =
        m_active_window &&
        *m_active_window == window_id;

    const auto previous_stacking_size =
        m_stacking_order.size();

    m_stacking_order.erase(
        std::remove(
            m_stacking_order.begin(),
            m_stacking_order.end(),
            window_id),
        m_stacking_order.end());

    if (active_window_removed)
        m_active_window.reset();

    m_last_revision = revision;

    notify_window_removed(window_id);

    if (active_window_removed)
    {
        notify_active_window_changed(
            m_active_window);
    }

    if (m_stacking_order.size() !=
        previous_stacking_size)
    {
        notify_stacking_order_changed(
            m_stacking_order);
    }

    return true;
}

bool KWinWindowBackend::
    publish_active_window(
        std::uint64_t revision,
        const std::optional<WindowId>
            &window_id)
{
    if (!accepts_incremental_revision(
            revision))
    {
        return false;
    }

    m_active_window.reset();

    if (window_id &&
        find_window(*window_id))
    {
        m_active_window = window_id;
    }

    for (auto &window :
         m_windows)
    {
        window.active =
            m_active_window &&
            window.id ==
                *m_active_window;
    }

    m_last_revision = revision;

    notify_active_window_changed(
        m_active_window);

    return true;
}

bool KWinWindowBackend::
    publish_stacking_order(
        std::uint64_t revision,
        const std::vector<WindowId>
            &stacking_order)
{
    if (!accepts_incremental_revision(
            revision))
    {
        return false;
    }

    m_stacking_order = stacking_order;
    m_last_revision = revision;

    notify_stacking_order_changed(
        m_stacking_order);

    return true;
}

bool KWinWindowBackend::
    publish_dock_surface_geometry(
        std::uint64_t revision,
        const std::optional<
            WindowIconGeometry> &geometry)
{
    if (!accepts_incremental_revision(
            revision))
    {
        return false;
    }

    const bool changed =
        m_dock_surface_geometry != geometry;

    m_dock_surface_geometry = geometry;
    m_last_revision = revision;

    if (changed)
    {
        notify_dock_surface_geometry_changed();
    }

    return true;
}

bool KWinWindowBackend::
    publish_dock_workarea_geometry(
        std::uint64_t revision,
        const std::optional<
            WindowIconGeometry> &geometry)
{
    if (!accepts_incremental_revision(
            revision))
    {
        return false;
    }

    set_dock_workarea_geometry(geometry);
    m_last_revision = revision;

    return true;
}

std::uint64_t
KWinWindowBackend::last_revision() const
{
    return m_last_revision;
}

ManagedWindow *
KWinWindowBackend::find_window(
    const WindowId &window_id)
{
    const auto window =
        std::find_if(
            m_windows.begin(),
            m_windows.end(),
            [&window_id](
                const ManagedWindow
                    &candidate)
            {
                return candidate.id ==
                       window_id;
            });

    return window == m_windows.end()
               ? nullptr
               : &*window;
}

const ManagedWindow *
KWinWindowBackend::find_window(
    const WindowId &window_id) const
{
    const auto window =
        std::find_if(
            m_windows.begin(),
            m_windows.end(),
            [&window_id](
                const ManagedWindow
                    &candidate)
            {
                return candidate.id ==
                       window_id;
            });

    return window == m_windows.end()
               ? nullptr
               : &*window;
}

bool KWinWindowBackend::
    accepts_incremental_revision(
        std::uint64_t revision) const
{
    return m_started &&
           m_registered &&
           m_connected &&
           !m_staged_revision &&
           revision > m_last_revision;
}

void KWinWindowBackend::apply_current_desktop(
    ManagedWindow &window) const
{
    if (m_current_desktop_id.empty() &&
        m_current_desktop_number == 0)
    {
        return;
    }

    if (window.desktop_ids.empty() &&
        window.desktop_numbers.empty())
    {
        window.on_current_desktop = true;
        return;
    }

    if (!m_current_desktop_id.empty() &&
        !window.desktop_ids.empty())
    {
        window.on_current_desktop =
            std::find(
                window.desktop_ids.begin(),
                window.desktop_ids.end(),
                m_current_desktop_id) !=
            window.desktop_ids.end();
        return;
    }

    window.on_current_desktop =
        m_current_desktop_number > 0 &&
        std::find(
            window.desktop_numbers.begin(),
            window.desktop_numbers.end(),
            m_current_desktop_number) !=
            window.desktop_numbers.end();
}

bool KWinWindowBackend::dispatch_command(
    KWinWindowCommandType type,
    const WindowId &window_id,
    bool state)
{
    if (!m_connected ||
        !m_command_handler ||
        !find_window(window_id))
    {
        return false;
    }

    return m_command_handler(
        KWinWindowCommand{
            window_id,
            type,
            state,
            {}});
}

void KWinWindowBackend::clear_state()
{
    m_windows.clear();
    m_staged_windows.clear();
    m_stacking_order.clear();

    m_active_window.reset();
    m_dock_surface_geometry.reset();
    set_dock_workarea_geometry(std::nullopt);
    m_staged_revision.reset();
    m_current_desktop_id.clear();
    m_current_desktop_number = 0;

    m_last_revision = 0;
    m_registered_protocol_version = 0;

    m_registered = false;
    m_connected = false;
}
