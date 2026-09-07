// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_process_application.cpp
//
// Implementation overview:
// Creates the desktop-neutral GApplication used for process uniqueness and
// activation and exported action dispatch, preserving window identity on
// X11 and Wayland surfaces.
//
// ------------------------------------------------------------

#include "application/dock_process_application.h"

#include <giomm/simpleaction.h>
#include <glibmm/main.h>

#include <gio/gio.h>
#include <gtk/gtk.h>

#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

namespace
{
    void apply_window_identity(
        Gtk::Window &window,
        const Glib::RefPtr<Gio::Application>
            &application)
    {
        auto gdk_window =
            gtk_widget_get_window(
                GTK_WIDGET(window.gobj()));

        if (!gdk_window)
            return;

        const char *application_id =
            g_application_get_application_id(
                application->gobj());
        const char *application_object_path =
            g_application_get_dbus_object_path(
                application->gobj());

        auto connection =
            g_application_get_dbus_connection(
                application->gobj());
        const char *unique_bus_name =
            connection
                ? g_dbus_connection_get_unique_name(
                      connection)
                : nullptr;

#ifdef GDK_WINDOWING_WAYLAND
        if (GDK_IS_WAYLAND_WINDOW(gdk_window))
        {
            gdk_wayland_window_set_application_id(
                gdk_window,
                application_id);
            gdk_wayland_window_set_dbus_properties_libgtk_only(
                gdk_window,
                application_id,
                nullptr,
                nullptr,
                nullptr,
                application_object_path,
                unique_bus_name);
            return;
        }
#endif

#ifdef GDK_WINDOWING_X11
        if (GDK_IS_X11_WINDOW(gdk_window))
        {
            gdk_x11_window_set_utf8_property(
                gdk_window,
                "_GTK_APPLICATION_ID",
                application_id);
            gdk_x11_window_set_utf8_property(
                gdk_window,
                "_GTK_APPLICATION_OBJECT_PATH",
                application_object_path);
            gdk_x11_window_set_utf8_property(
                gdk_window,
                "_GTK_UNIQUE_BUS_NAME",
                unique_bus_name);
        }
#endif
    }
}

Glib::RefPtr<Gio::Application>
DockProcessApplication::create()
{
    return Gio::Application::create(
        APPLICATION_ID);
}

void DockProcessApplication::bind_window_identity(
    Gtk::Window &window,
    const Glib::RefPtr<Gio::Application>
        &application)
{
    window.signal_realize().connect(
        [&window, application]()
        {
            apply_window_identity(
                window,
                application);
        });
}

void DockProcessApplication::register_actions(
    const Glib::RefPtr<Gio::Application> &application,
    const sigc::slot<void> &open_settings,
    const sigc::slot<void> &open_session,
    const sigc::slot<void> &show_about,
    const sigc::slot<void> &exit_docklight)
{
    // Defer modal callbacks and quit until the D-Bus request can return.
    const auto add_action = [&application](
        const char *name,
        const sigc::slot<void> &callback)
    {
        application->add_action(
            name,
            [callback]()
            {
                Glib::signal_idle().connect_once(callback);
            });
    };
    add_action("settings", open_settings);
    add_action("session", open_session);
    add_action("about", show_about);
    add_action("exit", exit_docklight);
}
