// Captures static window thumbnails through KWin's Wayland-safe screenshot
// interface without blocking the GTK main loop.

#pragma once

#include "managed_window.h"

#include <gdkmm/pixbuf.h>

#include <atomic>
#include <functional>
#include <memory>

class DockWindowThumbnailProvider
{
public:
    struct State
    {
        std::atomic<bool> alive{true};
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
