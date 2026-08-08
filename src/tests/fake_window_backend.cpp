// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// fake_window_backend.cpp
//
// Implementation overview:
// Implements the in-memory WindowBackend used by registry and
// application-controller tests.
//
// Important implementation decisions:
// - Mutators update owned state before emitting production-equivalent
//   signals.
// - Requested window actions are recorded or reflected deterministically.
// - No desktop session, compositor, or D-Bus connection is required.
//
// ------------------------------------------------------------

#include "fake_window_backend.h"

#include <algorithm>
#include <utility>

void FakeWindowBackend::start()
{
    set_connected(true);
}

void FakeWindowBackend::stop()
{
    set_connected(false);
}

std::string FakeWindowBackend::name() const
{
    return "Fake";
}

WindowBackendCapabilities
FakeWindowBackend::capabilities() const
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

bool FakeWindowBackend::connected() const
{
    return m_connected;
}

std::vector<ManagedWindow>
FakeWindowBackend::windows() const
{
    return m_windows;
}

std::vector<WindowId>
FakeWindowBackend::stacking_order() const
{
    return m_stacking_order;
}

std::optional<WindowId>
FakeWindowBackend::active_window() const
{
    return m_active_window;
}

std::optional<WindowIconGeometry>
FakeWindowBackend::
    dock_surface_geometry() const
{
    return m_dock_surface_geometry;
}

bool FakeWindowBackend::activate_window(
    const WindowId &window_id)
{
    if (!find_window(window_id))
        return false;

    set_active_window(window_id);
    return true;
}

bool FakeWindowBackend::present_windows(
    const std::vector<WindowId>
        &window_ids)
{
    if (window_ids.empty())
        return false;

    for (const auto &window_id :
         window_ids)
    {
        auto window =
            find_window(window_id);

        if (!window)
            return false;

        window->minimized = false;
        notify_window_updated(*window);

        m_stacking_order.erase(
            std::remove(
                m_stacking_order.begin(),
                m_stacking_order.end(),
                window_id),
            m_stacking_order.end());
        m_stacking_order.push_back(
            window_id);
    }

    notify_stacking_order_changed(
        m_stacking_order);
    set_active_window(
        window_ids.back());
    return true;
}

bool FakeWindowBackend::hide_windows(
    const std::vector<WindowId>
        &window_ids)
{
    if (window_ids.empty())
        return false;

    for (const auto &window_id :
         window_ids)
    {
        auto window =
            find_window(window_id);

        if (!window)
            return false;

        window->minimized = true;
        notify_window_updated(*window);
    }

    return true;
}

bool FakeWindowBackend::raise_window(
    const WindowId &window_id)
{
    if (!find_window(window_id))
        return false;

    m_stacking_order.erase(
        std::remove(
            m_stacking_order.begin(),
            m_stacking_order.end(),
            window_id),
        m_stacking_order.end());

    m_stacking_order.push_back(
        window_id);

    notify_stacking_order_changed(
        m_stacking_order);

    return true;
}

bool FakeWindowBackend::close_window(
    const WindowId &window_id)
{
    if (!find_window(window_id))
        return false;

    remove_window(window_id);
    return true;
}

bool FakeWindowBackend::
    set_window_minimized(
        const WindowId &window_id,
        bool minimized)
{
    auto window =
        find_window(window_id);

    if (!window)
        return false;

    window->minimized = minimized;
    notify_window_updated(*window);

    return true;
}

bool FakeWindowBackend::
    set_window_maximized(
        const WindowId &window_id,
        bool maximized)
{
    auto window =
        find_window(window_id);

    if (!window)
        return false;

    window->maximized = maximized;
    notify_window_updated(*window);

    return true;
}

bool FakeWindowBackend::
    set_window_icon_geometry(
        const WindowId &window_id,
        const WindowIconGeometry &geometry)
{
    return find_window(window_id) &&
           geometry.width > 0 &&
           geometry.height > 0;
}

void FakeWindowBackend::set_snapshot(
    std::vector<ManagedWindow> windows,
    std::vector<WindowId>
        stacking_order,
    std::optional<WindowId>
        active_window)
{
    m_windows = std::move(windows);
    m_stacking_order =
        std::move(stacking_order);
    m_active_window =
        std::move(active_window);
}

void FakeWindowBackend::add_window(
    const ManagedWindow &window)
{
    if (find_window(window.id))
    {
        update_window(window);
        return;
    }

    m_windows.push_back(window);
    m_stacking_order.push_back(
        window.id);

    notify_window_added(
        m_windows.back());
}

void FakeWindowBackend::update_window(
    const ManagedWindow &window)
{
    auto current =
        find_window(window.id);

    if (!current)
    {
        add_window(window);
        return;
    }

    *current = window;
    notify_window_updated(*current);
}

void FakeWindowBackend::remove_window(
    const WindowId &window_id)
{
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
        return;

    m_stacking_order.erase(
        std::remove(
            m_stacking_order.begin(),
            m_stacking_order.end(),
            window_id),
        m_stacking_order.end());

    if (m_active_window &&
        *m_active_window == window_id)
    {
        m_active_window.reset();
    }

    notify_window_removed(
        window_id);
}

void FakeWindowBackend::set_active_window(
    const std::optional<WindowId>
        &window_id)
{
    if (window_id &&
        !find_window(*window_id))
    {
        m_active_window.reset();
    }
    else
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

    notify_active_window_changed(
        m_active_window);
}

void FakeWindowBackend::set_stacking_order(
    const std::vector<WindowId>
        &stacking_order)
{
    m_stacking_order =
        stacking_order;

    notify_stacking_order_changed(
        m_stacking_order);
}

void FakeWindowBackend::set_connected(
    bool connected)
{
    if (m_connected == connected)
        return;

    m_connected = connected;

    notify_connection_changed(
        connected);
}

void FakeWindowBackend::
    set_dock_surface_geometry(
        const std::optional<
            WindowIconGeometry> &geometry)
{
    if (m_dock_surface_geometry == geometry)
        return;

    m_dock_surface_geometry = geometry;
    notify_dock_surface_geometry_changed();
}

ManagedWindow *
FakeWindowBackend::find_window(
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
