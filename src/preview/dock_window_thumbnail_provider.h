// Captures static window thumbnails through KWin's Wayland-safe screenshot
// interface without blocking the GTK main loop.

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
