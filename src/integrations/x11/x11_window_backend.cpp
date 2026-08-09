// ------------------------------------------------------------
// Docklight 6.0
//
// Generic EWMH/X11 window backend implemented through libwnck.
// ------------------------------------------------------------

#include "x11_window_backend.h"

#include <gdk/gdk.h>

#include <algorithm>
#include <cstdlib>

namespace
{

const char *safe_string(const char *value)
{
    return value ? value : "";
}

guint32 event_time()
{
    const guint32 timestamp = gtk_get_current_event_time();
    return timestamp == 0 ? GDK_CURRENT_TIME : timestamp;
}

}

X11WindowBackend::~X11WindowBackend()
{
    stop();
}

void X11WindowBackend::start()
{
    if (m_started)
        return;

    m_started = true;
    m_handle = wnck_handle_new(
        WNCK_CLIENT_TYPE_PAGER);
    m_screen = wnck_handle_get_default_screen(
        m_handle);

    if (!m_screen)
        return;

    wnck_screen_force_update(m_screen);

    // libwnck exposes the EWMH window manager name only after the initial
    // property round-trip. An X server without an EWMH WM is unsupported.
    m_connected =
        wnck_screen_get_window_manager_name(m_screen) != nullptr;

    g_signal_connect(m_screen, "window-opened",
                     G_CALLBACK(on_window_opened), this);
    g_signal_connect(m_screen, "window-closed",
                     G_CALLBACK(on_window_closed), this);
    g_signal_connect(m_screen, "active-window-changed",
                     G_CALLBACK(on_screen_value_changed), this);
    g_signal_connect(m_screen, "active-workspace-changed",
                     G_CALLBACK(on_screen_value_changed), this);
    g_signal_connect(m_screen, "window-stacking-changed",
                     G_CALLBACK(on_screen_changed), this);

    for (GList *item = wnck_screen_get_windows(m_screen);
         item;
         item = item->next)
    {
        watch_window(WNCK_WINDOW(item->data));
    }

    notify_connection_changed(m_connected);
    notify_snapshot_changed();
}

void X11WindowBackend::stop()
{
    if (!m_started)
        return;

    if (m_screen)
    {
        for (GList *item = wnck_screen_get_windows(m_screen);
             item;
             item = item->next)
        {
            g_signal_handlers_disconnect_by_data(item->data, this);
        }

        g_signal_handlers_disconnect_by_data(m_screen, this);
    }

    const bool was_connected = m_connected;
    m_screen = nullptr;
    g_clear_object(&m_handle);
    m_started = false;
    m_connected = false;

    if (was_connected)
        notify_connection_changed(false);
}

std::string X11WindowBackend::name() const
{
    return "X11/EWMH";
}

WindowBackendCapabilities X11WindowBackend::capabilities() const
{
    WindowBackendCapabilities result;
    result.can_activate = true;
    result.can_raise = true;
    result.can_close = true;
    result.can_minimize = true;
    result.can_maximize = true;
    result.provides_stacking_order = true;
    result.provides_virtual_desktops = true;
    result.provides_frame_geometry = true;
    result.accepts_icon_geometry = true;
    return result;
}

bool X11WindowBackend::connected() const
{
    return m_connected;
}

std::vector<ManagedWindow> X11WindowBackend::windows() const
{
    std::vector<ManagedWindow> result;

    if (!m_screen)
        return result;

    for (GList *item = wnck_screen_get_windows(m_screen);
         item;
         item = item->next)
    {
        result.push_back(managed_window(
            WNCK_WINDOW(item->data), m_screen));
    }

    return result;
}

std::vector<WindowId> X11WindowBackend::stacking_order() const
{
    std::vector<WindowId> result;

    if (!m_screen)
        return result;

    for (GList *item = wnck_screen_get_windows_stacked(m_screen);
         item;
         item = item->next)
    {
        result.push_back(window_id(WNCK_WINDOW(item->data)));
    }

    return result;
}

std::optional<WindowId> X11WindowBackend::active_window() const
{
    if (!m_screen)
        return std::nullopt;

    auto *window = wnck_screen_get_active_window(m_screen);
    return window
               ? std::optional<WindowId>(window_id(window))
               : std::nullopt;
}

std::optional<WindowIconGeometry>
X11WindowBackend::dock_surface_geometry() const
{
    return std::nullopt;
}

bool X11WindowBackend::activate_window(const WindowId &id)
{
    auto *window = find_window(id);
    if (!window)
        return false;

    wnck_window_activate(window, event_time());
    return true;
}

bool X11WindowBackend::present_windows(const std::vector<WindowId> &ids)
{
    WnckWindow *last = nullptr;
    for (const auto &id : ids)
    {
        auto *window = find_window(id);
        if (!window)
            continue;
        wnck_window_unminimize(window, event_time());
        last = window;
    }

    if (!last)
        return false;

    wnck_window_activate(last, event_time());
    return true;
}

bool X11WindowBackend::hide_windows(const std::vector<WindowId> &ids)
{
    bool handled = false;
    for (const auto &id : ids)
    {
        if (auto *window = find_window(id))
        {
            wnck_window_minimize(window);
            handled = true;
        }
    }
    return handled;
}

bool X11WindowBackend::raise_window(const WindowId &id)
{
    return activate_window(id);
}

bool X11WindowBackend::close_window(const WindowId &id)
{
    auto *window = find_window(id);
    if (!window)
        return false;
    wnck_window_close(window, event_time());
    return true;
}

bool X11WindowBackend::set_window_minimized(const WindowId &id,
                                             bool minimized)
{
    auto *window = find_window(id);
    if (!window)
        return false;

    if (minimized)
        wnck_window_minimize(window);
    else
        wnck_window_unminimize(window, event_time());
    return true;
}

bool X11WindowBackend::set_window_maximized(const WindowId &id,
                                             bool maximized)
{
    auto *window = find_window(id);
    if (!window)
        return false;

    if (maximized)
        wnck_window_maximize(window);
    else
        wnck_window_unmaximize(window);
    return true;
}

bool X11WindowBackend::set_window_icon_geometry(
    const WindowId &id,
    const WindowIconGeometry &geometry)
{
    auto *window = find_window(id);
    if (!window || geometry.width <= 0 || geometry.height <= 0)
        return false;

    wnck_window_set_icon_geometry(window,
                                  geometry.x,
                                  geometry.y,
                                  geometry.width,
                                  geometry.height);
    return true;
}

void X11WindowBackend::on_window_opened(WnckScreen *,
                                         WnckWindow *window,
                                         gpointer data)
{
    auto *backend = static_cast<X11WindowBackend *>(data);
    backend->watch_window(window);
    backend->snapshot_changed();
}

void X11WindowBackend::on_window_closed(WnckScreen *,
                                         WnckWindow *window,
                                         gpointer data)
{
    g_signal_handlers_disconnect_by_data(window, data);
    static_cast<X11WindowBackend *>(data)->snapshot_changed();
}

void X11WindowBackend::on_screen_changed(WnckScreen *, gpointer data)
{
    static_cast<X11WindowBackend *>(data)->snapshot_changed();
}

void X11WindowBackend::on_screen_value_changed(WnckScreen *,
                                                gpointer,
                                                gpointer data)
{
    static_cast<X11WindowBackend *>(data)->snapshot_changed();
}

void X11WindowBackend::on_window_changed(WnckWindow *, gpointer data)
{
    static_cast<X11WindowBackend *>(data)->snapshot_changed();
}

void X11WindowBackend::on_window_state_changed(WnckWindow *,
                                                WnckWindowState,
                                                WnckWindowState,
                                                gpointer data)
{
    static_cast<X11WindowBackend *>(data)->snapshot_changed();
}

void X11WindowBackend::watch_window(WnckWindow *window)
{
    if (!window)
        return;

    g_signal_connect(window, "name-changed",
                     G_CALLBACK(on_window_changed), this);
    g_signal_connect(window, "class-changed",
                     G_CALLBACK(on_window_changed), this);
    g_signal_connect(window, "workspace-changed",
                     G_CALLBACK(on_window_changed), this);
    g_signal_connect(window, "geometry-changed",
                     G_CALLBACK(on_window_changed), this);
    g_signal_connect(window, "state-changed",
                     G_CALLBACK(on_window_state_changed), this);
}

void X11WindowBackend::snapshot_changed()
{
    notify_snapshot_changed();
}

WnckWindow *X11WindowBackend::find_window(const WindowId &id) const
{
    if (!m_screen)
        return nullptr;

    for (GList *item = wnck_screen_get_windows(m_screen);
         item;
         item = item->next)
    {
        auto *window = WNCK_WINDOW(item->data);
        if (window_id(window) == id)
            return window;
    }
    return nullptr;
}

WindowId X11WindowBackend::window_id(WnckWindow *window)
{
    return std::to_string(wnck_window_get_xid(window));
}

ManagedWindow X11WindowBackend::managed_window(WnckWindow *window,
                                                WnckScreen *screen)
{
    ManagedWindow result;
    result.id = window_id(window);
    result.desktop_file_name = safe_string(
        wnck_window_get_class_group_name(window));
    result.caption = safe_string(wnck_window_get_name(window));
    result.icon_name = result.desktop_file_name;
    result.process_id = wnck_window_get_pid(window);
    result.active = wnck_screen_get_active_window(screen) == window;
    result.minimized = wnck_window_is_minimized(window);
    result.maximized = wnck_window_is_maximized(window);
    result.skip_taskbar = wnck_window_is_skip_tasklist(window);

    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    wnck_window_get_geometry(window, &x, &y, &width, &height);
    result.frame_geometry = {x, y, width, height};

    auto *workspace = wnck_window_get_workspace(window);
    auto *active_workspace = wnck_screen_get_active_workspace(screen);
    const bool pinned = wnck_window_is_pinned(window);
    result.on_current_desktop = pinned || workspace == active_workspace;

    if (workspace && !pinned)
    {
        result.desktop_numbers.push_back(
            static_cast<unsigned int>(wnck_workspace_get_number(workspace) + 1));
    }

    return result;
}
