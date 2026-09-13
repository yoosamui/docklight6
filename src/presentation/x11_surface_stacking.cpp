// ------------------------------------------------------------
// Docklight 6.0
//
// File: x11_surface_stacking.cpp
// Purpose: Request ordering for Docklight-owned X11/XWayland surfaces.
// Decisions: Use the EWMH pager source so focus-stealing prevention does not
// reject a dock's explicit stacking request. Ordinary GDK raises/restacks use
// application authority and are rejected by Muffin in the reproduced case.
// Without _NET_RESTACK_WINDOW, use a legacy sibling request. Only Marco and
// Metacity need ABOVE cleared to yield to the dock layer. Xfwm4 needs ABOVE
// retained so the preview stays ahead of application windows.
// Hyprland XWayland needs compositor ordering: X11 restacking does not change
// its visible stack. Raise only the named dock, asynchronously and without
// activation, when a magnified preview maps. Native layer-shell uses layers.
// Keep Xlib details outside GTK coordination and application WindowBackend.
// ------------------------------------------------------------

#include "x11_surface_stacking.h"
#include "presentation_selector.h"
#include "integrations/desktop_session_identity.h"

#include <gdk/gdkx.h>
#include <gio/gio.h>
#include <X11/Xlib.h>
#include <utility>

namespace
{

bool raise_hyprland_dock()
{
    GError *error = nullptr;
    auto *process = g_subprocess_new(
        static_cast<GSubprocessFlags>(
            G_SUBPROCESS_FLAGS_STDOUT_PIPE |
            G_SUBPROCESS_FLAGS_STDERR_PIPE),
        &error,
        "hyprctl", "dispatch", "alterzorder",
        "top,title:^(Docklight 6 Dock)$",
        nullptr);
    if (!process)
    {
        g_warning("Cannot request Hyprland dock stacking: %s",
                  error->message);
        g_clear_error(&error);
        return false;
    }

    // Do not block GTK's map/draw cycle on the compositor. This completion
    // owns no widget pointers and remains safe if the preview has closed.
    g_subprocess_communicate_utf8_async(
        process, nullptr, nullptr,
        [](GObject *source, GAsyncResult *result, gpointer)
        {
            gchar *output = nullptr;
            gchar *errors = nullptr;
            GError *failure = nullptr;
            const bool completed = g_subprocess_communicate_utf8_finish(
                G_SUBPROCESS(source), result, &output, &errors, &failure);
            if (!completed ||
                !g_subprocess_get_successful(G_SUBPROCESS(source)) ||
                !output || g_strcmp0(g_strstrip(output), "ok") != 0)
            {
                g_warning("Hyprland dock stacking failed: %s",
                          failure ? failure->message
                                  : (output ? output : "no reply"));
            }
            g_clear_error(&failure);
            g_free(output);
            g_free(errors);
        },
        nullptr);
    g_object_unref(process);
    return true;
}

}

bool request_x11_surface_below(
    const Glib::RefPtr<Gdk::Window> &surface,
    const Glib::RefPtr<Gdk::Window> &sibling)
{
    if (!surface || !sibling ||
        !GDK_IS_X11_DISPLAY(surface->get_display()->gobj()) ||
        surface->get_display() != sibling->get_display())
        return false;

    if (DesktopSessionIdentity::is_hyprland_wayland_session())
    {
        surface->get_display()->flush();
        // GTK's map signal precedes Hyprland's XWayland map processing.
        // A request sent immediately can succeed and then be undone when
        // the compositor inserts the preview. Allow that transaction to
        // settle; retain safe references and skip previews already hidden.
        using Surfaces = std::pair<
            Glib::RefPtr<Gdk::Window>, Glib::RefPtr<Gdk::Window>>;
        g_timeout_add_full(
            G_PRIORITY_DEFAULT, 80,
            [](gpointer data) -> gboolean
            {
                const auto &windows = *static_cast<Surfaces *>(data);
                if (windows.first->is_visible() &&
                    windows.second->is_visible())
                    raise_hyprland_dock();
                return G_SOURCE_REMOVE;
            },
            new Surfaces(surface, sibling),
            [](gpointer data) { delete static_cast<Surfaces *>(data); });
        return true;
    }

    if (!is_native_x11_presentation())
        return false;

    if (!gdk_x11_screen_supports_net_wm_hint(
            surface->get_screen()->gobj(),
            gdk_atom_intern_static_string("_NET_RESTACK_WINDOW")))
    {
        const char *wm_name = gdk_x11_screen_get_window_manager_name(
            surface->get_screen()->gobj());
        const bool needs_layer_workaround =
            g_strcmp0(wm_name, "Marco") == 0 ||
            g_strcmp0(wm_name, "Metacity") == 0;
        surface->set_keep_above(!needs_layer_workaround);
        surface->restack(sibling, false);
        surface->get_display()->flush();
        return true;
    }

    auto *display = surface->get_display()->gobj();
    auto *xdisplay = GDK_DISPLAY_XDISPLAY(display);
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.display = xdisplay;
    event.xclient.window = GDK_WINDOW_XID(surface->gobj());
    event.xclient.message_type = gdk_x11_get_xatom_by_name_for_display(
        display,
        "_NET_RESTACK_WINDOW");
    event.xclient.format = 32;
    event.xclient.data.l[0] = 2; // EWMH pager source: dock-owned surfaces.
    event.xclient.data.l[1] = GDK_WINDOW_XID(sibling->gobj());
    event.xclient.data.l[2] = Below;
    XSendEvent(
        xdisplay,
        GDK_WINDOW_XID(surface->get_screen()->get_root_window()->gobj()),
        False,
        SubstructureRedirectMask | SubstructureNotifyMask,
        &event);
    XFlush(xdisplay);
    return true;
}
