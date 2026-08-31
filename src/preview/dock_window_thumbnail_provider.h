// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window_thumbnail_provider.h
//
// Purpose:
// Declares asynchronous static window capture through native X11, KWin,
// GNOME Shell, or Hyprland's standard Wayland image-copy protocols.
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
#include <set>
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
        bool hyprland_capture = false;
    };

    using Callback = std::function<
        void(
            const WindowId &,
            const Glib::RefPtr<Gdk::Pixbuf> &)>;
    using LivePreviewsCallback =
        std::function<void(bool)>;

    DockWindowThumbnailProvider();
    ~DockWindowThumbnailProvider();

    void request(
        const WindowId &window_id,
        int target_width,
        int target_height,
        Callback callback,
        double x11_oversample = 2.0,
        bool x11_native_capture = false,
        bool x11_strict_composite = false);

    void set_x11_window_redirection(
        bool enabled);
    void set_x11_redirected_windows(
        const std::vector<WindowId> &window_ids);

    bool supports_gnome_live_previews() const;
    void set_gnome_preview_color(
        double red,
        double green,
        double blue,
        double alpha);
    void show_gnome_live_previews(
        const std::vector<GnomeLivePreviewRect>
            &previews,
        LivePreviewsCallback callback = {});
    void hold_gnome_live_preview_surface(
        LivePreviewsCallback callback);
    void forward_gnome_preview_primary_click(
        const WindowId &window_id,
        double normalized_x,
        double normalized_y);
    void hide_gnome_live_previews();

private:
    std::shared_ptr<State> m_state;
    bool m_gnome_live_previews_requested = false;
    void *m_x11_redirect_display = nullptr;
    std::set<unsigned long>
        m_x11_redirected_windows;
};
