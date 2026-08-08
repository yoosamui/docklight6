// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// dock_window_stream_provider.h
//
// Purpose:
// Declares persistent per-window live streams used by preview thumbnails.
//
// Responsibilities:
// - Start a stream for a target window and size.
// - Deliver decoded frames through a typed callback.
// - Stop individual streams or all provider activity.
//
// Dependencies and ownership:
// The provider owns an opaque implementation containing Wayland and
// PipeWire resources; callbacks receive shared pixbuf references.
//
// Design notes:
// Native streaming details are hidden from preview widgets.
//
// ------------------------------------------------------------

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
