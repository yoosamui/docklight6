#include "xfwm4_window_backend.h"

#include <gdk/gdkx.h>

Xfwm4WindowBackend::Xfwm4WindowBackend()
    : EwmhWindowBackend("Xfwm4/X11")
{
}

std::optional<bool>
Xfwm4WindowBackend::set_window_minimized_override(
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

    // libwnck implements unminimize as activation. With XFWM's "bring"
    // activation policy that reassigns every off-workspace window to the
    // active workspace. Mapping is XFWM's non-activating restore path, so
    // each window keeps its _NET_WM_DESKTOP assignment. The generic
    // present_windows() call still performs the one intended activation.
    gdk_x11_display_error_trap_push(display);
    XMapWindow(
        xdisplay,
        wnck_window_get_xid(window));
    XFlush(xdisplay);

    return gdk_x11_display_error_trap_pop(display) == 0;
}
