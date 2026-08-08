// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// dock_icon_renderer.h
//
// Purpose:
// Declares stateless pixbuf rendering helpers for dock hover effects.
//
// Responsibilities:
// - Create the standard highlighted icon.
// - Generate zoom animation frames.
// - Generate blurred halo animation frames.
//
// Dependencies and ownership:
// Input pixbufs are shared references and returned pixbufs are
// independently managed by GLib.
//
// Design notes:
// Animation scheduling belongs to DockItem; this module only creates
// pixels.
//
// ------------------------------------------------------------

#pragma once

#include <gdkmm/pixbuf.h>
#include <glibmm/refptr.h>

#include <vector>

namespace DockIconRenderer
{
Glib::RefPtr<Gdk::Pixbuf> create_standard_hover(
    const Glib::RefPtr<Gdk::Pixbuf> &source);

std::vector<Glib::RefPtr<Gdk::Pixbuf>> create_zoom_frames(
    const Glib::RefPtr<Gdk::Pixbuf> &source,
    int icon_size);

std::vector<Glib::RefPtr<Gdk::Pixbuf>> create_blur_frames(
    const Glib::RefPtr<Gdk::Pixbuf> &source,
    int icon_size);
}
