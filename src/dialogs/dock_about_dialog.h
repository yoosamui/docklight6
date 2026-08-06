// ------------------------------------------------------------
// Docklight 6.0
//
// Presents Docklight application information.
// ------------------------------------------------------------

#pragma once

#include <gdkmm/pixbuf.h>
#include <glibmm/refptr.h>

namespace Gtk
{
class Window;
}

namespace DockAboutDialog
{
void show(
    Gtk::Window &parent,
    const Glib::RefPtr<Gdk::Pixbuf> &icon);
}

