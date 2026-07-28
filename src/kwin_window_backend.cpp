#include "kwin_window_backend.h"

#include "kwin_integration_protocol.h"

#include <algorithm>

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

    // Window commands remain disabled until the command side of the
    // integration protocol is implemented.
    capabilities.provides_stacking_order =
        true;
    capabilities.provides_activities = true;
    capabilities.provides_virtual_desktops =
        true;
    capabilities.provides_frame_geometry =
        true;
    capabilities.provides_icons = true;

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

bool KWinWindowBackend::activate_window(
    const WindowId &)
{
    return false;
}

bool KWinWindowBackend::raise_window(
    const WindowId &)
{
    return false;
}

bool KWinWindowBackend::close_window(
    const WindowId &)
{
    return false;
}

bool KWinWindowBackend::
    set_window_minimized(
        const WindowId &,
        bool)
{
    return false;
}

bool KWinWindowBackend::
    set_window_maximized(
        const WindowId &,
        bool)
{
    return false;
}

bool KWinWindowBackend::register_integration(
    std::uint32_t protocol_version)
{
    if (!m_started ||
        protocol_version !=
            KWinIntegrationProtocol::VERSION)
    {
        return false;
    }

    const bool was_connected =
        m_connected;

    clear_state();
    m_registered = true;

    if (was_connected)
        notify_connection_changed(false);

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

    m_last_revision = revision;

    if (added)
        notify_window_added(*current);
    else
        notify_window_updated(*current);

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

void KWinWindowBackend::clear_state()
{
    m_windows.clear();
    m_staged_windows.clear();
    m_stacking_order.clear();

    m_active_window.reset();
    m_staged_revision.reset();

    m_last_revision = 0;

    m_registered = false;
    m_connected = false;
}
