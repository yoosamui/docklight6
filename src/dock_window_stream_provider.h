// Persistent KWin/PipeWire window streams for live preview thumbnails.

#pragma once

#include "windowing/managed_window.h"

#include <gdkmm/pixbuf.h>

#include <functional>
#include <memory>

class DockWindowStreamProvider
{
public:
    struct Impl;

    using Callback = std::function<
        void(
            const WindowId &,
            const Glib::RefPtr<Gdk::Pixbuf> &)>;

    DockWindowStreamProvider();
    ~DockWindowStreamProvider();

    bool available() const;
    bool start(
        const WindowId &window_id,
        int target_width,
        int target_height,
        Callback callback);
    void stop(const WindowId &window_id);
    void stop_all();

private:
    std::unique_ptr<Impl> m_impl;
};
