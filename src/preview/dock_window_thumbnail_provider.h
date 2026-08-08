// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window_thumbnail_provider.h
//
// Purpose:
// Declares asynchronous static window capture through KWin's Wayland-safe
// screenshot interface.
//
// Responsibilities:
// - Request a thumbnail for a managed window.
// - Convert completed captures to pixbufs.
// - Cancel callback delivery safely during teardown.
//
// Dependencies and ownership:
// The provider owns shared request state and its D-Bus connection;
// callbacks receive shared pixbuf references.
//
// Design notes:
// Static capture remains a fallback independent of live streaming.
//
// ------------------------------------------------------------

#pragma once

#include "windowing/managed_window.h"

#include <gio/gio.h>
#include <gdkmm/pixbuf.h>

#include <atomic>
#include <functional>
#include <memory>

class DockWindowThumbnailProvider
{
public:
    struct State
    {
        ~State()
        {
            if (connection)
                g_object_unref(connection);
        }

        std::atomic<bool> alive{true};
        GDBusConnection *connection = nullptr;
    };

    using Callback = std::function<
        void(
            const WindowId &,
            const Glib::RefPtr<Gdk::Pixbuf> &)>;

    DockWindowThumbnailProvider();
    ~DockWindowThumbnailProvider();

    void request(
        const WindowId &window_id,
        int target_width,
        int target_height,
        Callback callback);

private:
    std::shared_ptr<State> m_state;
};
