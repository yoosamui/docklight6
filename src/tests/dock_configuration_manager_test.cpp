// ------------------------------------------------------------
// Docklight 6.0
//
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

#include "config/dock_configuration_manager.h"

#include <glib.h>
#include <giomm/init.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include <cassert>
#include <filesystem>
#include <string>

int main()
{
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

    Gio::init();

    std::string config_path;

    {
        DockConfigurationManager initial_configuration(
            config_home);

        assert(initial_configuration.current().settings
                   .preview_card_height() == 512);
        assert(initial_configuration.current().settings
                   .autohide_hide_delay() == 1200);
        assert(initial_configuration.current().settings
                   .display_preview());
        assert(!initial_configuration.current().settings
                    .close_preview_after_activation());
        assert(initial_configuration.current().settings
                   .gradient_background());
        assert(initial_configuration.current().settings
                   .preview_color() == "#69aaff");

        config_path =
            initial_configuration.config_path();
    }

    // Versionless configurations used 0 as the old default. Verify that it
    // is upgraded once, then verify that an explicit 0 remains selectable.
    auto legacy_contents =
        Glib::file_get_contents(
            config_path);

    const std::string version_block =
        "# Internal configuration schema version.\n"
        "config_version = 1\n\n";
    const auto version_position =
        legacy_contents.find(version_block);

    assert(version_position != std::string::npos);

    legacy_contents.erase(
        version_position,
        version_block.size());

    const std::string new_preview_default =
        "preview_card_height = 512";
    const auto preview_position =
        legacy_contents.find(new_preview_default);

    assert(preview_position != std::string::npos);

    legacy_contents.replace(
        preview_position,
        new_preview_default.size(),
        "preview_card_height = 0");

    Glib::file_set_contents(
        config_path,
        legacy_contents);

    DockConfigurationManager configuration(
        config_home);

    assert(configuration.current().settings
               .preview_card_height() == 200);

    assert(configuration.save_setting(
        "preview_card_height",
        "0"));

    {
        DockConfigurationManager explicit_auto_configuration(
            config_home);

        assert(explicit_auto_configuration.current().settings
                   .preview_card_height() == 0);
    }

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
        "preview_color",
        "#445566"));
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
        "display_preview",
        "false"));
    assert(configuration.save_setting(
        "close_preview_after_activation",
        "true"));
    assert(configuration.save_setting(
        "manage_all_workspaces",
        "false"));
    assert(configuration.save_setting(
        "gradient_background",
        "false"));
    assert(configuration.save_setting(
        "icon_size",
        "64"));
    assert(configuration.save_setting(
        "preview_card_height",
        "256"));
    assert(configuration.save_setting(
        "preview_show_delay",
        "5000"));
    assert(configuration.save_setting(
        "autohide_hide_delay",
        "2500"));
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
               "preview_show_delay=5000") !=
           std::string::npos);
    assert(contents.find(
               "autohide_hide_delay=2500") !=
           std::string::npos);
    assert(contents.find(
               "preview_color=#445566") !=
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
               "display_preview=false") !=
           std::string::npos);
    assert(contents.find(
               "close_preview_after_activation=true") !=
           std::string::npos);
    assert(contents.find(
               "manage_all_workspaces=false") !=
           std::string::npos);
    assert(contents.find(
               "gradient_background=false") !=
           std::string::npos);

    DockConfigurationManager reloaded(
        config_home);
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
    assert(current.settings.preview_color() ==
           "#445566");
    assert(!current.settings
                .home_icon_enabled());
    assert(current.settings.home_icon_path() ==
           "/tmp/My Custom Icon.png");
    assert(!current.settings
                .display_tooltips());
    assert(!current.settings
                .display_preview());
    assert(current.settings
               .close_preview_after_activation());
    assert(!current.settings
                .manage_all_workspaces());
    assert(!current.settings
                .gradient_background());
    assert(current.settings.icon_size() == 64);
    assert(current.settings
               .preview_card_height() == 256);
    assert(current.settings
               .preview_show_delay() == 5000);
    assert(current.settings
               .autohide_hide_delay() == 2500);
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
