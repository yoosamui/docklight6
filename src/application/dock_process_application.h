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
// menus or session inhibitors. Gio::Application owns exported actions,
// uniqueness, activation, and the main loop, while GTK is initialized by the
// process entry point. This avoids GtkApplication's synchronous desktop-portal
// session proxy during graphical-session restarts.
//
// ------------------------------------------------------------

#pragma once

#include <giomm/application.h>
#include <gtkmm/window.h>
#include <sigc++/slot.h>

namespace DockProcessApplication
{
    constexpr char APPLICATION_ID[] =
        "org.docklight6";

    Glib::RefPtr<Gio::Application> create();

    void register_actions(
        const Glib::RefPtr<Gio::Application> &application,
        const sigc::slot<void> &open_settings,
        const sigc::slot<void> &open_session,
        const sigc::slot<void> &show_about,
        const sigc::slot<void> &exit_docklight);

    // Publishes the identity that GtkApplication normally adds when a window
    // is realized, without enabling GtkApplication session management.
    void bind_window_identity(
        Gtk::Window &window,
        const Glib::RefPtr<Gio::Application>
            &application);
}
