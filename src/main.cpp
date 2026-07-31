// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// main.cpp
//
// Implementation overview:
// Provides the Docklight process entry point, command-line monitor
// listing, subsystem startup, and construction of the main dock window.
//
// Important implementation decisions:
// - Configuration and monitor selection are established before the UI.
// - Window-system integration is optional outside supported sessions.
// - Long-lived managers remain in main for the GTK application lifetime.
// - Configuration and monitor changes are forwarded as complete updates.
//
// ------------------------------------------------------------

#include <gtkmm.h>

#include "dock_configuration_manager.h"
#include "dock_monitor_manager.h"
#include "dock_window.h"
#include "window_system_controller.h"
#include "config.h"

#include <string>

namespace
{

bool take_list_monitors_option(
    int &argc,
    char *argv[])
{
    bool list_monitors = false;
    int write_index = 1;

    for (int read_index = 1;
         read_index < argc;
         ++read_index)
    {
        if (std::string(argv[read_index]) ==
            "--list-monitors")
        {
            list_monitors = true;
            continue;
        }

        argv[write_index++] =
            argv[read_index];
    }

    argc = write_index;
    argv[argc] = nullptr;

    return list_monitors;
}

}

// Establishes long-lived managers before creating DockWindow, then forwards
// configuration and monitor changes for the GTK application lifetime.
int main(int argc, char *argv[])
{
    const bool list_monitors =
        take_list_monitors_option(
            argc,
            argv);

    auto app = Gtk::Application::create(
        argc,
        argv,
        "org.docklight6");

    // Ensure an existing configuration receives newly introduced settings
    // even when this invocation only lists monitors.
    DockConfigurationManager configuration;

    if (list_monitors)
    {
        DockMonitorManager monitors;
        monitors.print_available_monitors();

        return monitors
                       .available_monitors()
                       .empty()
                   ? 1
                   : 0;
    }

    // Emit GApplication::startup before manually attaching the dock window.
    // run(window) normally performs this registration, but that overload
    // cannot be used because a monitor move temporarily hides the window.
    app->register_application();

    if (app->is_remote())
    {
        g_message(
            "DockLight is already running");
        return app->run();
    }

    g_message(
        "%s starting",
        PACKAGE_STRING);

    g_message(
        "Dock configuration loaded: %s",
        configuration.config_path().c_str());

    WindowSystemController window_system;
    window_system.start();

    auto css = Gtk::CssProvider::create();
    const std::string style_path =
        std::string(SOURCE_DIR) + "/style.css";

    try
    {
        css->load_from_path(
            style_path);

        Gtk::StyleContext::add_provider_for_screen(
            Gdk::Screen::get_default(),
            css,
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

        g_message(
            "Dock style loaded: %s",
            style_path.c_str());
    }
    catch (const Glib::Error &error)
    {
        g_warning(
            "Cannot load DockLight style: %s",
            error.what().c_str());
    }

    DockMonitorManager monitors(
        configuration
            .current()
            .settings
            .monitor());

    DockWindow window(
        configuration.current(),
        monitors.selected_monitor(),
        window_system.registry());

    configuration.signal_changed().connect(
        [&window, &monitors](
            const DockConfiguration
                &updated_configuration)
        {
            window.apply_configuration(
                updated_configuration);

            monitors.set_requested_monitor(
                updated_configuration
                    .settings
                    .monitor());
        });

    monitors.signal_monitor_changed().connect(
        sigc::mem_fun(
            window,
            &DockWindow::set_monitor));

    configuration.start_monitoring();
    monitors.start_monitoring();

    // Gtk::Application::run(window) returns as soon as that window is hidden.
    // gtk_layer_set_monitor() temporarily unmaps the layer surface while
    // moving it, so using the convenience overload would terminate DockLight
    // during every runtime monitor change.
    app->add_window(window);
    app->hold();
    window.show();

    g_message("DockLight is ready");

    return app->run();
}
