// ------------------------------------------------------------
// Docklight 6.0
//
// Stateless pixbuf rendering for dock hover effects.
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

