#include "dock_configuration_manager.h"

#include <giomm/file.h>
#include <glib.h>
#include <glibmm/fileutils.h>
#include <glibmm/keyfile.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>

namespace
{

constexpr int MIN_ICON_SIZE = 32;
constexpr int MAX_ICON_SIZE = 128;
constexpr int MIN_CORNER_RADIUS = 2;
constexpr unsigned int RELOAD_DELAY_MS = 200;

const char *CONFIG_FILENAME = "docklight.conf";
const char *DOCK_GROUP = "dock";

const char *MONITOR_SETTING_TEMPLATE = R"(# Monitor used by the dock.
# Empty uses default: primary
# Run "docklight6 --list-monitors" to show accepted identifiers
monitor =

)";

const char *HOVER_EFFECT_SETTING_TEMPLATE = R"(# Effect shown while the pointer is over an icon.
# Empty uses default: standard
# Valid values: standard, zoom, pixels, glow
# zoom, pixels, and glow currently use the standard effect
hover_effect =

)";

const char *CONFIG_TEMPLATE = R"([dock]
# Monitor used by the dock.
# Empty uses default: primary
# Run "docklight6 --list-monitors" to show accepted identifiers
monitor =

# Effect shown while the pointer is over an icon.
# Empty uses default: standard
# Valid values: standard, zoom, pixels, glow
# zoom, pixels, and glow currently use the standard effect
hover_effect =

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
# The hiding behavior will be implemented later.
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
        else if (hover_effect == "pixels")
        {
            candidate.settings.set_hover_effect(
                DockHoverEffect::pixels);
        }
        else if (hover_effect == "glow")
        {
            candidate.settings.set_hover_effect(
                DockHoverEffect::glow);
        }
        else
        {
            g_warning(
                "Invalid [dock] hover_effect '%s'; "
                "keeping the previous value",
                hover_effect.c_str());
        }

        const auto icon_size =
            value_for(
                key_file,
                "icon_size");

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
