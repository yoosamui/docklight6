#include "openbox_window_backend.h"

#include <gdk/gdkx.h>

OpenboxWindowBackend::OpenboxWindowBackend()
    : EwmhWindowBackend("Openbox/X11")
{
}

WindowBackendCapabilities
OpenboxWindowBackend::capabilities() const
{
    auto result =
        EwmhWindowBackend::capabilities();
    result.thumbnail_policy =
        WindowThumbnailPolicy::
            redirect_and_cache_mapped_windows;
    result.thumbnails_require_compositor = true;
    return result;
}

bool OpenboxWindowBackend::hide_windows(
    const std::vector<WindowId>
        &window_ids)
{
    if (window_ids.empty())
        return false;

    std::vector<WnckWindow *> windows;
    windows.reserve(window_ids.size());

    // Resolve the entire snapshot before dispatching anything. A group action
    // must not leave half of an application visible merely because one stale
    // XID disappeared between the registry snapshot and this call.
    for (const auto &window_id :
         window_ids)
    {
        auto *window =
            find_window(window_id);
        if (!window)
            return false;

        windows.push_back(window);
    }

    return set_windows_hidden(
        windows,
        true);
}

std::optional<bool>
OpenboxWindowBackend::set_window_minimized_override(
    WnckWindow *window,
    bool minimized)
{
    if (!window)
        return false;

    // Unlike libwnck unminimize, changing Openbox's HIDDEN state does not
    // activate the window or move it to the current workspace. This lets the
    // controller restore every member before presenting one intended target.
    return set_windows_hidden(
        {window},
        minimized);
}

bool OpenboxWindowBackend::set_windows_hidden(
    const std::vector<WnckWindow *>
        &windows,
    bool hidden)
{
    if (windows.empty())
        return false;

    auto *display =
        gdk_display_get_default();
    if (!display ||
        !GDK_IS_X11_DISPLAY(display))
    {
        return false;
    }

    auto *xdisplay =
        gdk_x11_display_get_xdisplay(
            display);
    if (!xdisplay)
        return false;

    const Atom net_wm_state =
        XInternAtom(
            xdisplay,
            "_NET_WM_STATE",
            False);
    const Atom hidden_atom =
        XInternAtom(
            xdisplay,
            "_NET_WM_STATE_HIDDEN",
            False);
    const Window root =
        DefaultRootWindow(xdisplay);

    bool accepted = true;

    gdk_x11_display_error_trap_push(
        display);

    // Openbox handles HIDDEN through its native EWMH state path. Send every
    // member as one pager-originated batch instead of using libwnck's legacy
    // activation-based helpers independently for each window. This keeps
    // focus and workspace changes from truncating either group transition.
    for (auto *window : windows)
    {
        XEvent event{};
        event.xclient.type =
            ClientMessage;
        event.xclient.serial = 0;
        event.xclient.send_event = True;
        event.xclient.display = xdisplay;
        event.xclient.window =
            wnck_window_get_xid(window);
        event.xclient.message_type =
            net_wm_state;
        event.xclient.format = 32;
        event.xclient.data.l[0] =
            hidden
                ? 1 // _NET_WM_STATE_ADD
                : 0; // _NET_WM_STATE_REMOVE
        event.xclient.data.l[1] =
            static_cast<long>(hidden_atom);
        event.xclient.data.l[2] = 0;
        event.xclient.data.l[3] = 2; // Pager source indication
        event.xclient.data.l[4] = 0;

        accepted =
            XSendEvent(
                xdisplay,
                root,
                False,
                SubstructureRedirectMask |
                    SubstructureNotifyMask,
                &event) != 0 &&
            accepted;
    }

    XFlush(xdisplay);

    return gdk_x11_display_error_trap_pop(
               display) == 0 &&
           accepted;
}
