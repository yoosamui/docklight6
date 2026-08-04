// ------------------------------------------------------------
// Docklight 6.0
//
// Presents and persists the Docklight settings dialog.
// ------------------------------------------------------------

#pragma once

#include <gdkmm/pixbuf.h>
#include <glibmm/refptr.h>

namespace Gtk
{
class Window;
}

namespace DockSettingsDialog
{
void show(
    Gtk::Window &parent,
    const Glib::RefPtr<Gdk::Pixbuf> &icon);
}

