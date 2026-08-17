// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 Juan González  <https://github.com/yoosamui>
//
// Author & Maintainer: Juan González
// Development Pair: OpenAI ChatGPT 5.6 Sol
//
// This file is part of Docklight.
//
// Docklight is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Docklight is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
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

#include "config/dock_configuration_manager.h"
#include "application/dock_runtime_info.h"
#include "monitors/dock_monitor_manager.h"
#include "dock/dock_window.h"
#include "docklight_log.h"
#include "integrations/window_system_controller.h"
#include "presentation/presentation_selector.h"
#include "presentation/docklight_surface_identity.h"
#include "config.h"

#include <gtkmm.h>
#include <glib/gi18n.h>

#include <clocale>
#include <libintl.h>
#include <memory>
#include <optional>
#include <string>

namespace
{

    DockConfiguration effective_configuration(
        const DockConfiguration &configuration,
        bool window_previews_available)
    {
        auto effective = configuration;
        if (!window_previews_available)
        {
            effective.settings.set_display_preview(
                false);
        }

        return effective;
    }

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
    DocklightLog::initialize();

    // Initialize the process locale and translation domain before GTK creates
    // widgets whose labels may be translated.
    std::setlocale(LC_ALL, "");
    bindtextdomain(
        GETTEXT_PACKAGE,
        LOCALEDIR);
    bind_textdomain_codeset(
        GETTEXT_PACKAGE,
        "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    const bool list_monitors =
        take_list_monitors_option(
            argc,
            argv);

    std::optional<PresentationMode>
        requested_presentation;
    std::string presentation_error;

    if (!take_presentation_option(
            argc,
            argv,
            requested_presentation,
            presentation_error))
    {
        g_printerr(
            "docklight6: %s\n",
            presentation_error.c_str());
        return 2;
    }

    const auto presentation =
        select_presentation(
            requested_presentation);

    if (!prepare_presentation(
            presentation,
            presentation_error))
    {
        g_printerr(
            "docklight6: %s\n",
            presentation_error.c_str());
        return 2;
    }

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

    DocklightLog::startup(
        "%s starting",
        PACKAGE_STRING);

    const std::string presentation_source_suffix =
        presentation.source == "default"
            ? ""
            : " (" + presentation.source + ")";

    DocklightLog::startup(
        "Presentation backend: %s%s",
        actual_presentation_backend_name(),
        presentation_source_suffix.c_str());

    DocklightLog::startup(
        "Presentation mode: %s",
        presentation_mode_name(
            presentation.mode));

    DocklightLog::startup(
        "Dock configuration loaded: %s",
        configuration.config_path().c_str());

    WindowSystemController window_system;
    window_system.start();

    const auto &window_system_details =
        window_system.details();
    const DockRuntimeInfo runtime_info{
        presentation_mode_name(
            presentation.mode),
        configuration.config_path(),
        window_system_details.desktop,
        window_system_details.window_manager,
        window_system_details.compositor,
        window_system_details.backend};

    const bool window_previews_available =
        window_system.window_previews_available();
    const bool disable_configured_previews =
        !window_previews_available &&
        configuration
            .current()
            .settings
            .display_preview();

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

        DocklightLog::startup(
            "Dock style loaded: %s",
            style_path.c_str());
    }
    catch (const Glib::Error &error)
    {
        DocklightLog::startup(
            "Cannot load DockLight style: %s",
            error.what().c_str());
    }

    DockMonitorManager monitors(
        configuration
            .current()
            .settings
            .monitor());

    const auto initial_configuration =
        effective_configuration(
            configuration.current(),
            window_previews_available);

    DockWindow window(
        initial_configuration,
        monitors.selected_monitor(),
        window_system.registry(),
        runtime_info);

    // A second invocation is delivered to this primary process as an
    // application activation. Treat it as an explicit request to reveal the
    // existing dock instead of silently exiting while the dock is hidden.
    app->signal_activate().connect(
        [&window]()
        {
            window.request_reveal();
        });

    configuration.signal_changed().connect(
        [&window,
         &monitors,
         window_previews_available](
            const DockConfiguration
                &updated_configuration)
        {
            const auto effective =
                effective_configuration(
                    updated_configuration,
                    window_previews_available);
            window.apply_configuration(
                effective);

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

    if (disable_configured_previews)
    {
        configuration.save_setting(
            "display_preview",
            "false");
    }

    // Gtk::Application::run(window) returns as soon as that window is hidden.
    // gtk_layer_set_monitor() temporarily unmaps the layer surface while
    // moving it, so using the convenience overload would terminate DockLight
    // during every runtime monitor change.
    app->add_window(window);
    app->hold();
    window.show();

    DocklightLog::startup(
        "DockLight is ready");

    std::unique_ptr<Gtk::MessageDialog>
        compositor_warning;
    if (!window_previews_available)
    {
        compositor_warning =
            std::make_unique<Gtk::MessageDialog>(
                window,
                _("Window previews are disabled"),
                false,
                Gtk::MESSAGE_WARNING,
                Gtk::BUTTONS_OK,
                false);
        compositor_warning->set_title(
            _("DockLight"));
        gtk_window_set_role(
            GTK_WINDOW(
                compositor_warning->gobj()),
            DocklightSurfaceIdentity::
                COMPOSITOR_WARNING_ROLE);
        compositor_warning->set_secondary_text(
            _("Openbox requires an X11 compositor for complete window "
              "previews. Display Preview is disabled. Start compton or "
              "picom, then enable "
              "Display Preview in DockLight Settings."));
        compositor_warning
            ->signal_response()
            .connect(
                [&compositor_warning](int)
                {
                    compositor_warning->hide();
                });
        compositor_warning->show_all();
    }

    return app->run();
}
