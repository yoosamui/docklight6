// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// dock_configuration_manager_test.cpp
//
// Test scope:
// Verifies configuration creation, parsing, validation, migration of
// missing settings, and persistence of supported values.
//
// Temporary user directories isolate filesystem side effects from the
// developer's real Docklight configuration.
//
// ------------------------------------------------------------

#include "dock_configuration_manager.h"

#include <glib.h>
#include <giomm/init.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include <cassert>
#include <filesystem>
#include <string>

int main()
{
    Gio::init();

    GError *error = nullptr;
    char *temporary_directory =
        g_dir_make_tmp(
            "docklight-config-test-XXXXXX",
            &error);

    assert(!error);
    assert(temporary_directory);

    const std::string config_home =
        temporary_directory;

    g_free(temporary_directory);

    assert(Glib::setenv(
        "XDG_CONFIG_HOME",
        config_home,
        true));

    DockConfigurationManager configuration;

    assert(configuration.save_setting(
        "monitor",
        "test-monitor"));
    assert(configuration.save_setting(
        "hover_effect",
        "blur"));
    assert(configuration.save_setting(
        "indicator",
        "dots"));
    assert(configuration.save_setting(
        "indicator_color",
        "#112233"));
    assert(configuration.save_setting(
        "home_icon_enabled",
        "false"));
    assert(configuration.save_setting(
        "home_icon_path",
        "/tmp/My Custom Icon.png"));
    assert(configuration.save_setting(
        "display_tooltips",
        "false"));
    assert(configuration.save_setting(
        "manage_all_workspaces",
        "false"));
    assert(configuration.save_setting(
        "icon_size",
        "64"));
    assert(configuration.save_setting(
        "preview_card_height",
        "256"));
    assert(configuration.save_setting(
        "location",
        "top"));
    assert(configuration.save_setting(
        "rounded_corners",
        "false"));
    assert(configuration.save_setting(
        "corner_radius",
        "-1"));
    assert(configuration.save_setting(
        "alignment",
        "end"));
    assert(configuration.save_setting(
        "autohide",
        "intellihide"));

    const auto contents =
        Glib::file_get_contents(
            configuration.config_path());

    assert(contents.find(
               "# Icon size in pixels.") !=
           std::string::npos);
    assert(contents.find(
               "preview_card_height=256") !=
           std::string::npos);
    assert(contents.find(
               "home_icon_enabled=false") !=
           std::string::npos);
    assert(contents.find(
               "home_icon_path=/tmp/My Custom Icon.png") !=
           std::string::npos);
    assert(contents.find(
               "display_tooltips=false") !=
           std::string::npos);
    assert(contents.find(
               "manage_all_workspaces=false") !=
           std::string::npos);

    DockConfigurationManager reloaded;
    const auto &current =
        reloaded.current();

    assert(current.settings.monitor() ==
           "test-monitor");
    assert(current.settings.hover_effect() ==
           DockHoverEffect::blur);
    assert(current.settings.indicator() ==
           DockIndicator::dots);
    assert(current.settings.indicator_color() ==
           "#112233");
    assert(!current.settings
                .home_icon_enabled());
    assert(current.settings.home_icon_path() ==
           "/tmp/My Custom Icon.png");
    assert(!current.settings
                .display_tooltips());
    assert(!current.settings
                .manage_all_workspaces());
    assert(current.settings.icon_size() == 64);
    assert(current.settings
               .preview_card_height() == 256);
    assert(current.layout_request.location ==
           DockLocation::top);
    assert(!current.layout_request
                .rounded_corners);
    assert(current.layout_request
               .corner_radius == -1);
    assert(current.layout_request.alignment ==
           DockAlignment::end);
    assert(current.layout_request.autohide ==
           DockAutohide::intellihide);

    std::filesystem::remove_all(
        config_home);

    return 0;
}
