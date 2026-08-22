#include "marco_window_backend.h"

#include <gdk/gdkx.h>

MarcoWindowBackend::MarcoWindowBackend()
    : EwmhWindowBackend("Marco/Metacity X11")
{
}

WindowBackendCapabilities
MarcoWindowBackend::capabilities() const
{
    auto result =
        EwmhWindowBackend::capabilities();
    result.thumbnail_policy =
        WindowThumbnailPolicy::cache_mapped_windows;
    return result;
}

std::optional<bool>
MarcoWindowBackend::set_window_minimized_override(
    WnckWindow *window,
    bool minimized)
{
    if (minimized)
        return std::nullopt;

    if (!window)
        return false;

    auto *display = gdk_display_get_default();
    if (!display || !GDK_IS_X11_DISPLAY(display))
        return false;

    auto *xdisplay =
        gdk_x11_display_get_xdisplay(display);
    if (!xdisplay)
        return false;

    auto *workspace =
        wnck_window_get_workspace(window);

    // libwnck implements unminimize as an activation request, which cannot
    // restore a multi-workspace group in place. A MapRequest restores without
    // focus, but Metacity and Marco move an off-workspace mapped window to the
    // active workspace. Reassert the cached original workspace in the same X11
    // request stream before present_windows() performs the one intended
    // activation.
    gdk_x11_display_error_trap_push(display);
    XMapWindow(
        xdisplay,
        wnck_window_get_xid(window));

    if (workspace)
    {
        wnck_window_move_to_workspace(
            window,
            workspace);
    }

    XFlush(xdisplay);

    return gdk_x11_display_error_trap_pop(display) == 0;
}
