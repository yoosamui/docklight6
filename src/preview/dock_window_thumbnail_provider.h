// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window_thumbnail_provider.h
//
// Purpose:
// Declares asynchronous static window capture through native X11, KWin's
// screenshot interface, or Docklight's GNOME Shell integration.
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
#include <vector>

struct GnomeLivePreviewRect
{
    WindowId window_id;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

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
        bool x11 = false;
        bool gnome_shell_capture = false;
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
        Callback callback,
        double x11_oversample = 2.0,
        bool x11_native_capture = false,
        bool x11_xfwm_mode = false);

    bool supports_gnome_live_previews() const;
    void show_gnome_live_previews(
        const std::vector<GnomeLivePreviewRect>
            &previews);
    void forward_gnome_preview_primary_click(
        const WindowId &window_id,
        double normalized_x,
        double normalized_y);
    void hide_gnome_live_previews();

private:
    std::shared_ptr<State> m_state;
};
