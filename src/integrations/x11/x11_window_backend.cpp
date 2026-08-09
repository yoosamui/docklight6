// ------------------------------------------------------------
// Docklight 6.0
//
// Generic EWMH/X11 window backend implemented through libwnck.
// ------------------------------------------------------------

#include "x11_window_backend.h"

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gio/gio.h>

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
    if (timestamp != 0)
        return timestamp;

    // Dock clicks finish after their animation and preview clicks are
    // dispatched from an idle callback. At that point GTK no longer has a
    // current event. Muffin treats CurrentTime as an untrusted activation
    // request and may reject it, so retain the timestamp of the actual user
    // interaction recorded by GDK's X11 display.
    auto *display = gdk_display_get_default();
    if (display && GDK_IS_X11_DISPLAY(display))
    {
        const guint32 user_time =
            gdk_x11_display_get_user_time(display);
        if (user_time != 0)
            return user_time;
    }

    return GDK_CURRENT_TIME;
}

bool is_muffin(WnckScreen *screen)
{
    if (!screen)
        return false;

    const char *manager =
        wnck_screen_get_window_manager_name(screen);
    if (!manager)
        return false;

    auto *normalized = g_ascii_strdown(manager, -1);
    const bool result =
        normalized &&
        std::string(normalized).find("muffin") !=
            std::string::npos;
    g_free(normalized);
    return result;
}

bool cinnamon_eval_boolean(const std::string &script)
{
    GError *error = nullptr;
    auto *connection =
        g_bus_get_sync(
            G_BUS_TYPE_SESSION,
            nullptr,
            &error);
    if (!connection)
    {
        g_warning(
            "Cannot connect to Cinnamon: %s",
            error ? error->message : "unknown error");
        g_clear_error(&error);
        return false;
    }

    auto *reply =
        g_dbus_connection_call_sync(
            connection,
            "org.Cinnamon",
            "/org/Cinnamon",
            "org.Cinnamon",
            "Eval",
            g_variant_new("(s)", script.c_str()),
            G_VARIANT_TYPE("(bs)"),
            G_DBUS_CALL_FLAGS_NONE,
            1000,
            nullptr,
            &error);
    g_object_unref(connection);

    if (!reply)
    {
        g_warning(
            "Cinnamon window action failed: %s",
            error ? error->message : "unknown error");
        g_clear_error(&error);
        return false;
    }

    gboolean success = FALSE;
    const char *result = nullptr;
    g_variant_get(reply, "(b&s)", &success, &result);
    const bool accepted =
        success && result && g_str_equal(result, "true");
    if (!accepted)
    {
        g_warning(
            "Cinnamon did not accept window action: %s",
            result ? result : "no result");
    }
    g_variant_unref(reply);
    return accepted;
}

bool muffin_activate_windows(
    WnckScreen *screen,
    const std::vector<WnckWindow *> &windows)
{
    if (!is_muffin(screen) || windows.empty())
        return false;

    std::string script = "(() => { const ids = [";
    for (std::size_t index = 0;
         index < windows.size();
         ++index)
    {
        if (index > 0)
            script += ',';
        script += std::to_string(
            wnck_window_get_xid(windows[index]));
    }

    script +=
        "]; const wins = global.get_window_actors()"
        ".map(a => a.meta_window)"
        ".filter(w => ids.includes(w.get_xwindow()));"
        "if (wins.length === 0) return false;"
        "const target = wins[wins.length - 1];"
        "wins.forEach(w => w.unminimize());"
        "target.get_workspace().activate_with_focus("
        "target, global.get_current_time());"
        "return true; })()";

    return cinnamon_eval_boolean(script);
}

bool muffin_set_minimized(
    WnckScreen *screen,
    WnckWindow *window,
    bool minimized)
{
    if (!is_muffin(screen) || !window)
        return false;

    const std::string script =
        "(() => { const xid = " +
        std::to_string(wnck_window_get_xid(window)) +
        "; const actor = global.get_window_actors()"
        ".find(a => a.meta_window.get_xwindow() === xid);"
        "if (!actor) return false; actor.meta_window." +
        std::string(minimized ? "minimize" : "unminimize") +
        "(); return true; })()";

    return cinnamon_eval_boolean(script);
}

bool is_application_auxiliary(
    WnckWindow *window,
    WnckScreen *screen)
{
    if (!window || !screen ||
        !wnck_window_is_skip_tasklist(window) ||
        !wnck_window_is_above(window) ||
        wnck_window_get_window_type(window) !=
            WNCK_WINDOW_UTILITY)
    {
        return false;
    }

    const auto process_id =
        wnck_window_get_pid(window);
    const std::string class_group =
        safe_string(
            wnck_window_get_class_group_name(
                window));

    for (GList *item =
             wnck_screen_get_windows(screen);
         item;
         item = item->next)
    {
        auto *candidate =
            WNCK_WINDOW(item->data);

        if (candidate == window ||
            wnck_window_is_skip_tasklist(candidate))
        {
            continue;
        }

        const bool same_process =
            process_id > 0 &&
            wnck_window_get_pid(candidate) ==
                process_id;
        const bool same_class =
            !class_group.empty() &&
            class_group == safe_string(
                wnck_window_get_class_group_name(
                    candidate));

        if (same_process || same_class)
            return true;
    }

    return false;
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
                     G_CALLBACK(on_active_workspace_changed), this);
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
    m_pending_activation_window_ids.clear();
    m_pending_activation_workspace = -1;
    m_pending_activation_timestamp = 0;
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

    if (is_muffin(m_screen))
    {
        // Never fall through to wnck_window_activate() on Muffin: it moves
        // an off-workspace window onto the current workspace.
        return muffin_activate_windows(
            m_screen, {window});
    }

    const guint32 timestamp = event_time();
    if (defer_activation_until_workspace(
            {id}, window, timestamp))
    {
        return true;
    }

    wnck_window_activate(window, timestamp);
    return true;
}

bool X11WindowBackend::present_windows(const std::vector<WindowId> &ids)
{
    std::vector<WnckWindow *> windows;
    windows.reserve(ids.size());

    for (const auto &id : ids)
    {
        auto *window = find_window(id);
        if (window)
            windows.push_back(window);
    }

    if (windows.empty())
        return false;

    if (is_muffin(m_screen))
    {
        return muffin_activate_windows(
            m_screen, windows);
    }

    const guint32 timestamp = event_time();
    auto *target = windows.back();

    if (defer_activation_until_workspace(
            ids, target, timestamp))
    {
        return true;
    }

    for (auto *window : windows)
    {
        if (wnck_window_is_minimized(window))
            wnck_window_unminimize(window, timestamp);
    }

    wnck_window_activate(target, timestamp);
    return true;
}

bool X11WindowBackend::defer_activation_until_workspace(
    const std::vector<WindowId> &window_ids,
    WnckWindow *target,
    guint32 timestamp)
{
    if (!target || !m_screen ||
        wnck_window_is_pinned(target))
    {
        return false;
    }

    auto *workspace =
        wnck_window_get_workspace(target);

    if (!workspace ||
        workspace ==
            wnck_screen_get_active_workspace(m_screen))
    {
        return false;
    }

    m_pending_activation_window_ids = window_ids;
    m_pending_activation_workspace =
        wnck_workspace_get_number(workspace);
    m_pending_activation_timestamp = timestamp;

    wnck_workspace_activate(workspace, timestamp);
    return true;
}

void X11WindowBackend::complete_pending_activation()
{
    if (!m_screen ||
        m_pending_activation_window_ids.empty())
    {
        return;
    }

    auto *workspace =
        wnck_screen_get_active_workspace(m_screen);

    if (!workspace ||
        wnck_workspace_get_number(workspace) !=
            m_pending_activation_workspace)
    {
        return;
    }

    const auto window_ids =
        std::move(m_pending_activation_window_ids);
    const guint32 timestamp =
        m_pending_activation_timestamp;

    m_pending_activation_workspace = -1;
    m_pending_activation_timestamp = 0;

    WnckWindow *target = nullptr;
    for (const auto &id : window_ids)
    {
        auto *window = find_window(id);
        if (!window)
            continue;

        if (wnck_window_is_minimized(window))
            wnck_window_unminimize(window, timestamp);

        target = window;
    }

    if (target)
        wnck_window_activate(target, timestamp);
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

    if (is_muffin(m_screen))
        return muffin_set_minimized(
            m_screen, window, minimized);

    if (minimized)
        wnck_window_minimize(window);
    else
    {
        const guint32 timestamp = event_time();
        if (!defer_activation_until_workspace(
                {id}, window, timestamp))
        {
            wnck_window_unminimize(window, timestamp);
        }
    }
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

void X11WindowBackend::on_active_workspace_changed(
    WnckScreen *,
    WnckWorkspace *,
    gpointer data)
{
    auto *backend =
        static_cast<X11WindowBackend *>(data);
    backend->snapshot_changed();
    backend->complete_pending_activation();
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
    result.include_when_skip_taskbar =
        is_application_auxiliary(window, screen);

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
