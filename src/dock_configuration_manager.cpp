// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// dock_configuration_manager.cpp
//
// Implementation overview:
// Implements configuration-file creation, schema migration,
// validation, persistence, and change monitoring.
//
// Important implementation decisions:
// - Invalid values fall back through centralized parsing rules.
// - Missing settings are appended without rewriting user choices.
// - Directory events are debounced before the file is reloaded.
// - Change signals are emitted only for a materially new snapshot.
//
// ------------------------------------------------------------

#include "dock_configuration_manager.h"

#include <giomm/file.h>
#include <glib.h>
#include <glibmm/fileutils.h>
#include <glibmm/keyfile.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>
#include <gdkmm/rgba.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>

namespace
{

constexpr int MIN_ICON_SIZE = 32; // Smallest accepted dock icon size
constexpr int MAX_ICON_SIZE = 128; // Largest accepted dock icon size
constexpr int MIN_CORNER_RADIUS = 2; // Smallest explicit dock corner radius
constexpr unsigned int RELOAD_DELAY_MS = 200; // Delay before reloading changed settings

const char *CONFIG_FILENAME = "docklight.conf"; // Per-user configuration filename
const char *DOCK_GROUP = "dock"; // Configuration group containing dock settings

// Configuration block added when the monitor setting is missing.
const char *MONITOR_SETTING_TEMPLATE = R"(# Monitor used by the dock.
# Empty uses default: primary
# Run "docklight6 --list-monitors" to show accepted identifiers
monitor =

)";

// Configuration block added when the hover-effect setting is missing.
const char *HOVER_EFFECT_SETTING_TEMPLATE = R"(# Effect shown while the pointer is over an icon.
# Empty uses default: standard
# Valid values: standard, zoom, blur
hover_effect =

)";

// Configuration block added when the indicator setting is missing.
const char *INDICATOR_SETTING_TEMPLATE = R"(# Running-window indicator style.
# Valid values: lines, dots
indicator = lines

)";

// Configuration block added when the indicator-color setting is missing.
const char *INDICATOR_COLOR_SETTING_TEMPLATE = R"(# Running-window indicator fill color.
# Accepts GTK colors such as #rrggbb, rgb(), rgba(), or a named color
indicator_color = #69aaff

)";

// Configuration block added when the home-icon visibility setting is missing.
const char *HOME_ICON_ENABLED_SETTING_TEMPLATE = R"(# Display the static DockLight home icon.
# Valid values: true, false
home_icon_enabled = true

)";

// Configuration block added when the home-icon path setting is missing.
const char *HOME_ICON_PATH_SETTING_TEMPLATE = R"(# Custom image for the static home icon.
# Empty uses the built-in DockLight icon
home_icon_path =

)";

// Configuration block added when the tooltip visibility setting is missing.
const char *DISPLAY_TOOLTIPS_SETTING_TEMPLATE = R"(# Display application tooltips while hovering over dock icons.
# Valid values: true, false
display_tooltips = true

)";

// Configuration block added when the workspace-management setting is missing.
const char *MANAGE_ALL_WORKSPACES_SETTING_TEMPLATE = R"(# Manage application windows across all virtual workspaces.
# When false, icon actions and mouse-wheel cycling use only the current workspace.
# Valid values: true, false
manage_all_workspaces = true

)";

// Complete configuration written when no user configuration exists.
const char *CONFIG_TEMPLATE = R"([dock]
# Monitor used by the dock.
# Empty uses default: primary
# Run "docklight6 --list-monitors" to show accepted identifiers
monitor =

# Effect shown while the pointer is over an icon.
# Empty uses default: standard
# Valid values: standard, zoom, blur
hover_effect =

# Running-window indicator style.
# Valid values: lines, dots
indicator = lines

# Running-window indicator fill color.
# Accepts GTK colors such as #rrggbb, rgb(), rgba(), or a named color
indicator_color = #69aaff

# Display the static DockLight home icon.
# Valid values: true, false
home_icon_enabled = true

# Custom image for the static home icon.
# Empty uses the built-in DockLight icon
home_icon_path =

# Display application tooltips while hovering over dock icons.
# Valid values: true, false
display_tooltips = true

# Manage application windows across all virtual workspaces.
# When false, icon actions and mouse-wheel cycling use only the current workspace.
# Valid values: true, false
manage_all_workspaces = true

# Icon size in pixels.
# Empty uses default: 46
# Valid range: 32 to 128
icon_size =

# Dock screen edge.
# Empty uses default: bottom
# Valid values: bottom, left, top, right
location =

# Enable rounded dock corners.
# Empty uses default: true
# Valid values: true, false
rounded_corners =

# Dock corner radius in pixels.
# Empty uses default: 6
# -1 selects an automatic radius
# Otherwise valid range: 2 to icon_size / 2
corner_radius =

# Dock alignment along its screen edge.
# Empty uses default: center
# Valid values: start, center, end, fill
alignment =

# Dock hiding mode.
# Empty uses default: none
# Valid values: none, autohide, intellihide
# autohide hides after the pointer leaves and reveals at the screen edge.
# intellihide hides only while a current-desktop window overlaps the dock.
autohide =
)";

std::string trimmed(const Glib::ustring &input)
{
    std::string value = input.raw();

    auto first =
        std::find_if_not(
            value.begin(),
            value.end(),
            [](unsigned char character)
            {
                return std::isspace(character);
            });

    auto last =
        std::find_if_not(
            value.rbegin(),
            value.rend(),
            [](unsigned char character)
            {
                return std::isspace(character);
            })
            .base();

    if (first >= last)
        return {};

    return std::string(
        first,
        last);
}

std::string normalized(const Glib::ustring &input)
{
    std::string value =
        trimmed(input);

    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(
                std::tolower(character));
        });

    return value;
}

std::optional<int> parse_integer(
    const std::string &value)
{
    try
    {
        std::size_t parsed_characters = 0;

        const int parsed =
            std::stoi(
                value,
                &parsed_characters);

        if (parsed_characters != value.size())
            return {};

        return parsed;
    }
    catch (const std::exception &)
    {
        return {};
    }
}

std::optional<bool> parse_boolean(
    const std::string &value)
{
    if (value == "true")
        return true;

    if (value == "false")
        return false;

    return {};
}

std::string value_for(
    const Glib::KeyFile &key_file,
    const char *key)
{
    if (!key_file.has_key(
            DOCK_GROUP,
            key))
    {
        return {};
    }

    return normalized(
        key_file.get_value(
            DOCK_GROUP,
            key));
}

std::string text_value_for(
    const Glib::KeyFile &key_file,
    const char *key)
{
    if (!key_file.has_key(
            DOCK_GROUP,
            key))
    {
        return {};
    }

    return trimmed(
        key_file.get_value(
            DOCK_GROUP,
            key));
}

bool same_configuration(
    const DockConfiguration &left,
    const DockConfiguration &right)
{
    return left.settings.monitor() ==
               right.settings.monitor() &&
           left.settings.icon_size() ==
               right.settings.icon_size() &&
           left.settings.hover_effect() ==
               right.settings.hover_effect() &&
           left.settings.indicator() ==
               right.settings.indicator() &&
           left.settings.indicator_color() ==
               right.settings.indicator_color() &&
           left.settings.home_icon_enabled() ==
               right.settings.home_icon_enabled() &&
           left.settings.home_icon_path() ==
               right.settings.home_icon_path() &&
           left.settings.display_tooltips() ==
               right.settings.display_tooltips() &&
           left.settings.manage_all_workspaces() ==
               right.settings.manage_all_workspaces() &&
           left.settings.minimum_bottom_workarea_inset() ==
               right.settings.minimum_bottom_workarea_inset() &&
           left.layout_request.location ==
               right.layout_request.location &&
           left.layout_request.alignment ==
               right.layout_request.alignment &&
           left.layout_request.autohide ==
               right.layout_request.autohide &&
           left.layout_request.rounded_corners ==
               right.layout_request.rounded_corners &&
           left.layout_request.corner_radius ==
               right.layout_request.corner_radius &&
           left.layout_request.size.length ==
               right.layout_request.size.length &&
           left.layout_request.size.thickness ==
               right.layout_request.size.thickness;
}

}

DockConfigurationManager::DockConfigurationManager()
{
    m_config_directory =
        Glib::build_filename(
            Glib::get_user_config_dir(),
            "docklight6");

    m_config_path =
        Glib::build_filename(
            m_config_directory,
            CONFIG_FILENAME);

    ensure_config_file();
    ensure_setting(
        "monitor",
        MONITOR_SETTING_TEMPLATE);
    ensure_setting(
        "hover_effect",
        HOVER_EFFECT_SETTING_TEMPLATE);
    ensure_setting(
        "indicator",
        INDICATOR_SETTING_TEMPLATE);
    ensure_setting(
        "indicator_color",
        INDICATOR_COLOR_SETTING_TEMPLATE);
    ensure_setting(
        "home_icon_enabled",
        HOME_ICON_ENABLED_SETTING_TEMPLATE);
    ensure_setting(
        "home_icon_path",
        HOME_ICON_PATH_SETTING_TEMPLATE);
    ensure_setting(
        "display_tooltips",
        DISPLAY_TOOLTIPS_SETTING_TEMPLATE);
    ensure_setting(
        "manage_all_workspaces",
        MANAGE_ALL_WORKSPACES_SETTING_TEMPLATE);
    reload();
}

const DockConfiguration &
DockConfigurationManager::current() const
{
    return m_current;
}

const std::string &
DockConfigurationManager::config_path() const
{
    return m_config_path;
}

sigc::signal<
    void,
    const DockConfiguration &> &
DockConfigurationManager::signal_changed()
{
    return m_signal_changed;
}

void DockConfigurationManager::start_monitoring()
{
    if (m_monitor)
        return;

    try
    {
        auto directory =
            Gio::File::create_for_path(
                m_config_directory);

        m_monitor =
            directory->monitor_directory(
                Gio::FILE_MONITOR_WATCH_MOVES);

        m_monitor->signal_changed().connect(
            sigc::mem_fun(
                *this,
                &DockConfigurationManager::
                    on_directory_changed));
    }
    catch (const Glib::Error &error)
    {
        g_warning(
            "Cannot monitor dock configuration '%s': %s",
            m_config_path.c_str(),
            error.what().c_str());
    }
}

bool DockConfigurationManager::save_setting(
    const std::string &key,
    const std::string &value)
{
    if (key.empty())
        return false;

    Glib::KeyFile key_file;

    try
    {
        if (!key_file.load_from_file(
                m_config_path,
                Glib::KEY_FILE_KEEP_COMMENTS))
        {
            return false;
        }

        key_file.set_value(
            DOCK_GROUP,
            key,
            value);

        Glib::file_set_contents(
            m_config_path,
            key_file.to_data());

        g_message(
            "Saved [dock] %s = %s",
            key.c_str(),
            value.c_str());

        return true;
    }
    catch (const Glib::Error &error)
    {
        g_warning(
            "Cannot save [dock] setting '%s' "
            "to '%s': %s",
            key.c_str(),
            m_config_path.c_str(),
            error.what().c_str());
        return false;
    }
}

void DockConfigurationManager::ensure_config_file()
{
    try
    {
        auto directory =
            Gio::File::create_for_path(
                m_config_directory);

        if (!directory->query_exists())
            directory->make_directory_with_parents();

        auto config_file =
            Gio::File::create_for_path(
                m_config_path);

        if (!config_file->query_exists())
        {
            Glib::file_set_contents(
                m_config_path,
                CONFIG_TEMPLATE);

            g_message(
                "Created dock configuration: %s",
                m_config_path.c_str());
        }
    }
    catch (const Glib::Error &error)
    {
        g_warning(
            "Cannot create dock configuration '%s': %s",
            m_config_path.c_str(),
            error.what().c_str());
    }
}

void DockConfigurationManager::ensure_setting(
    const char *key,
    const char *setting_template)
{
    Glib::KeyFile key_file;

    try
    {
        if (!key_file.load_from_file(
                m_config_path,
                Glib::KEY_FILE_KEEP_COMMENTS) ||
            !key_file.has_group(DOCK_GROUP) ||
            key_file.has_key(
                DOCK_GROUP,
                key))
        {
            return;
        }

        auto contents =
            Glib::file_get_contents(
                m_config_path);

        const auto group_position =
            contents.find("[dock]");

        if (group_position ==
            std::string::npos)
        {
            return;
        }

        const auto line_end =
            contents.find(
                '\n',
                group_position);

        const auto insert_position =
            line_end == std::string::npos
                ? contents.size()
                : line_end + 1;

        contents.insert(
            insert_position,
            setting_template);

        Glib::file_set_contents(
            m_config_path,
            contents);

        g_message(
            "Added '%s' setting to dock configuration",
            key);
    }
    catch (const Glib::Error &error)
    {
        g_warning(
            "Cannot update dock configuration '%s': %s",
            m_config_path.c_str(),
            error.what().c_str());
    }
}

// Builds and validates a complete configuration snapshot from disk.
// Keeping fallback and normalization rules here ensures every consumer sees
// the same typed values and never interprets the key file independently.
void DockConfigurationManager::reload()
{
    ensure_config_file();

    Glib::KeyFile key_file;

    try
    {
        if (!key_file.load_from_file(
                m_config_path,
                Glib::KEY_FILE_KEEP_COMMENTS))
        {
            g_warning(
                "Cannot load dock configuration '%s'; "
                "keeping the previous configuration",
                m_config_path.c_str());
            return;
        }
    }
    catch (const Glib::Error &error)
    {
        g_warning(
            "Invalid dock configuration '%s': %s; "
            "keeping the previous configuration",
            m_config_path.c_str(),
            error.what().c_str());
        return;
    }

    DockConfiguration candidate =
        m_current;

    const DockConfiguration defaults;

    if (!key_file.has_group(DOCK_GROUP))
    {
        g_warning(
            "Missing [dock] group in '%s'; using defaults",
            m_config_path.c_str());
        candidate = defaults;
    }
    else
    {
        const auto monitor =
            text_value_for(
                key_file,
                "monitor");

        candidate.settings.set_monitor(
            monitor.empty()
                ? defaults.settings.monitor()
                : monitor);

        const auto hover_effect =
            value_for(
                key_file,
                "hover_effect");

        if (hover_effect.empty() ||
            hover_effect == "standard")
        {
            candidate.settings.set_hover_effect(
                DockHoverEffect::standard);
        }
        else if (hover_effect == "zoom")
        {
            candidate.settings.set_hover_effect(
                DockHoverEffect::zoom);
        }
        else if (hover_effect == "blur")
        {
            candidate.settings.set_hover_effect(
                DockHoverEffect::blur);
        }
        else
        {
            g_warning(
                "Invalid [dock] hover_effect '%s'; "
                "keeping the previous value",
                hover_effect.c_str());
        }

        const auto indicator =
            value_for(
                key_file,
                "indicator");

        if (indicator.empty() ||
            indicator == "lines")
        {
            candidate.settings.set_indicator(
                DockIndicator::lines);
        }
        else if (indicator == "dots")
        {
            candidate.settings.set_indicator(
                DockIndicator::dots);
        }
        else
        {
            g_warning(
                "Invalid [dock] indicator '%s'; "
                "keeping the previous value",
                indicator.c_str());
        }

        const auto indicator_color =
            text_value_for(
                key_file,
                "indicator_color");

        if (indicator_color.empty())
        {
            candidate.settings
                .set_indicator_color(
                    defaults.settings
                        .indicator_color());
        }
        else
        {
            Gdk::RGBA color;

            if (color.set(indicator_color))
            {
                candidate.settings
                    .set_indicator_color(
                        indicator_color);
            }
            else
            {
                g_warning(
                    "Invalid [dock] indicator_color '%s'; "
                    "keeping '%s'",
                    indicator_color.c_str(),
                    candidate.settings
                        .indicator_color()
                        .c_str());
            }
        }

        const auto icon_size =
            value_for(
                key_file,
                "icon_size");

        const auto home_icon_enabled =
            value_for(
                key_file,
                "home_icon_enabled");

        if (home_icon_enabled.empty())
        {
            candidate.settings
                .set_home_icon_enabled(
                    defaults.settings
                        .home_icon_enabled());
        }
        else if (const auto enabled =
                     parse_boolean(
                         home_icon_enabled))
        {
            candidate.settings
                .set_home_icon_enabled(
                    *enabled);
        }
        else
        {
            g_warning(
                "Invalid [dock] home_icon_enabled '%s'; "
                "keeping the previous value",
                home_icon_enabled.c_str());
        }

        candidate.settings.set_home_icon_path(
            text_value_for(
                key_file,
                "home_icon_path"));

        const auto display_tooltips =
            value_for(
                key_file,
                "display_tooltips");

        if (display_tooltips.empty())
        {
            candidate.settings
                .set_display_tooltips(
                    defaults.settings
                        .display_tooltips());
        }
        else if (const auto enabled =
                     parse_boolean(
                         display_tooltips))
        {
            candidate.settings
                .set_display_tooltips(
                    *enabled);
        }
        else
        {
            g_warning(
                "Invalid [dock] display_tooltips '%s'; "
                "keeping the previous value",
                display_tooltips.c_str());
        }

        const auto manage_all_workspaces =
            value_for(
                key_file,
                "manage_all_workspaces");

        if (manage_all_workspaces.empty())
        {
            candidate.settings
                .set_manage_all_workspaces(
                    defaults.settings
                        .manage_all_workspaces());
        }
        else if (const auto enabled =
                     parse_boolean(
                         manage_all_workspaces))
        {
            candidate.settings
                .set_manage_all_workspaces(
                    *enabled);
        }
        else
        {
            g_warning(
                "Invalid [dock] manage_all_workspaces '%s'; "
                "keeping the previous value",
                manage_all_workspaces.c_str());
        }

        if (icon_size.empty())
        {
            candidate.settings.set_icon_size(
                defaults.settings.icon_size());
        }
        else
        {
            const auto parsed =
                parse_integer(icon_size);

            if (parsed &&
                *parsed >= MIN_ICON_SIZE &&
                *parsed <= MAX_ICON_SIZE)
            {
                candidate.settings.set_icon_size(
                    *parsed);
            }
            else
            {
                g_warning(
                    "Invalid [dock] icon_size '%s'; "
                    "keeping %d",
                    icon_size.c_str(),
                    candidate.settings.icon_size());
            }
        }

        const auto location =
            value_for(
                key_file,
                "location");

        if (location.empty())
            candidate.layout_request.location =
                defaults.layout_request.location;
        else if (location == "bottom")
            candidate.layout_request.location =
                DockLocation::bottom;
        else if (location == "left")
            candidate.layout_request.location =
                DockLocation::left;
        else if (location == "top")
            candidate.layout_request.location =
                DockLocation::top;
        else if (location == "right")
            candidate.layout_request.location =
                DockLocation::right;
        else
            g_warning(
                "Invalid [dock] location '%s'; "
                "keeping the previous value",
                location.c_str());

        const auto rounded_corners =
            value_for(
                key_file,
                "rounded_corners");

        if (rounded_corners.empty())
            candidate.layout_request.rounded_corners =
                defaults.layout_request.rounded_corners;
        else if (rounded_corners == "true")
            candidate.layout_request.rounded_corners =
                true;
        else if (rounded_corners == "false")
            candidate.layout_request.rounded_corners =
                false;
        else
            g_warning(
                "Invalid [dock] rounded_corners '%s'; "
                "keeping the previous value",
                rounded_corners.c_str());

        const auto corner_radius =
            value_for(
                key_file,
                "corner_radius");

        if (corner_radius.empty())
        {
            candidate.layout_request.corner_radius =
                defaults.layout_request.corner_radius;
        }
        else
        {
            const auto parsed =
                parse_integer(corner_radius);

            const int maximum_radius =
                candidate.settings.icon_size() / 2;

            if (parsed &&
                (*parsed == -1 ||
                 (*parsed >= MIN_CORNER_RADIUS &&
                  *parsed <= maximum_radius)))
            {
                candidate.layout_request.corner_radius =
                    *parsed;
            }
            else
            {
                const int previous_radius =
                    candidate.layout_request.corner_radius;

                const bool previous_radius_is_valid =
                    previous_radius == -1 ||
                    (previous_radius >= MIN_CORNER_RADIUS &&
                     previous_radius <= maximum_radius);

                if (!previous_radius_is_valid)
                {
                    candidate.layout_request.corner_radius =
                        defaults.layout_request.corner_radius;
                }

                g_warning(
                    "Invalid [dock] corner_radius '%s'; "
                    "expected -1 or %d..%d; keeping %d",
                    corner_radius.c_str(),
                    MIN_CORNER_RADIUS,
                    maximum_radius,
                    candidate.layout_request.corner_radius);
            }
        }

        const auto alignment =
            value_for(
                key_file,
                "alignment");

        if (alignment.empty())
            candidate.layout_request.alignment =
                defaults.layout_request.alignment;
        else if (alignment == "start")
            candidate.layout_request.alignment =
                DockAlignment::start;
        else if (alignment == "center")
            candidate.layout_request.alignment =
                DockAlignment::center;
        else if (alignment == "end")
            candidate.layout_request.alignment =
                DockAlignment::end;
        else if (alignment == "fill")
            candidate.layout_request.alignment =
                DockAlignment::fill;
        else
            g_warning(
                "Invalid [dock] alignment '%s'; "
                "keeping the previous value",
                alignment.c_str());

        const auto autohide =
            value_for(
                key_file,
                "autohide");

        if (autohide.empty())
            candidate.layout_request.autohide =
                defaults.layout_request.autohide;
        else if (autohide == "none")
            candidate.layout_request.autohide =
                DockAutohide::none;
        else if (autohide == "autohide")
            candidate.layout_request.autohide =
                DockAutohide::autohide;
        else if (autohide == "intellihide")
            candidate.layout_request.autohide =
                DockAutohide::intellihide;
        else
            g_warning(
                "Invalid [dock] autohide '%s'; "
                "keeping the previous value",
                autohide.c_str());
    }

    if (same_configuration(
            candidate,
            m_current))
    {
        return;
    }

    m_current = candidate;
    m_signal_changed.emit(
        m_current);

    g_message(
        "Dock configuration reloaded: %s",
        m_config_path.c_str());
}

void DockConfigurationManager::schedule_reload()
{
    if (m_reload_timer.connected())
        m_reload_timer.disconnect();

    m_reload_timer =
        Glib::signal_timeout().connect(
            [this]()
            {
                reload();
                return false;
            },
            RELOAD_DELAY_MS);
}

bool DockConfigurationManager::is_config_file(
    const Glib::RefPtr<Gio::File> &file) const
{
    return file &&
           file->get_basename() ==
               CONFIG_FILENAME;
}

void DockConfigurationManager::on_directory_changed(
    const Glib::RefPtr<Gio::File> &file,
    const Glib::RefPtr<Gio::File> &other_file,
    Gio::FileMonitorEvent)
{
    if (is_config_file(file) ||
        is_config_file(other_file))
    {
        schedule_reload();
    }
}
