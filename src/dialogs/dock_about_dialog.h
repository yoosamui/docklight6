// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_about_dialog.h
//
// Purpose:
// Declares the entry point for presenting Docklight application
// information.
//
// Responsibilities:
// - Build the About dialog for a parent window.
// - Display the supplied application icon.
// - Hide toolkit construction details from callers.
//
// Dependencies and ownership:
// The function borrows the parent and shares the referenced pixbuf; GTK
// owns dialog widgets during presentation.
//
// Design notes:
// A namespace function avoids persistent dialog state.
//
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
