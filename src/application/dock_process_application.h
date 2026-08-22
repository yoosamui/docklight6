// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_process_application.h
//
// Purpose:
// Defines Docklight's process-level application lifecycle independently from
// GTK session management.
//
// Design notes:
// Docklight uses plain Gtk::Window surfaces and does not use GtkApplication
// menus, actions, or session inhibitors. Gio::Application therefore owns
// uniqueness, activation, and the main loop, while GTK is initialized by the
// process entry point. This avoids GtkApplication's synchronous desktop-portal
// session proxy during graphical-session restarts.
//
// ------------------------------------------------------------

#pragma once

#include <giomm/application.h>
#include <gtkmm/window.h>

namespace DockProcessApplication
{
    constexpr char APPLICATION_ID[] =
        "org.docklight6";

    Glib::RefPtr<Gio::Application> create();

    // Publishes the identity that GtkApplication normally adds when a window
    // is realized, without enabling GtkApplication session management.
    void bind_window_identity(
        Gtk::Window &window,
        const Glib::RefPtr<Gio::Application>
            &application);
}
