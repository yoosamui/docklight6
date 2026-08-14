// ------------------------------------------------------------
// Docklight 6.0
//
// Keeps presentation transport selection orthogonal to GNOME, Plasma, and
// X11 window integration selection.
// ------------------------------------------------------------

#include "presentation_selector.h"

#include <gdk/gdkwayland.h>
#include <gdk/gdkx.h>
#include <glib.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <vector>

namespace
{

std::string normalized(std::string value)
{
    value.erase(
        value.begin(),
        std::find_if_not(
            value.begin(),
            value.end(),
            [](unsigned char character)
            {
                return std::isspace(character);
            }));

    value.erase(
        std::find_if_not(
            value.rbegin(),
            value.rend(),
            [](unsigned char character)
            {
                return std::isspace(character);
            })
            .base(),
        value.end());

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

std::vector<PresentationMode>
read_configured_modes(
    const std::string &path)
{
    std::ifstream input(path);
    std::string line;

    while (std::getline(input, line))
    {
        line = normalized(line);
        if (line.empty() || line[0] == '#')
            continue;

        constexpr char prefix[] = "mode=";
        if (line.compare(
                0,
                sizeof(prefix) - 1,
                prefix) == 0)
        {
            std::vector<PresentationMode> modes;
            auto values = line.substr(
                sizeof(prefix) - 1);
            std::size_t begin = 0;

            while (begin <= values.size())
            {
                const auto end = values.find(',', begin);
                const auto mode = parse_presentation_mode(
                    values.substr(begin, end - begin));
                if (mode)
                    modes.push_back(*mode);
                if (end == std::string::npos)
                    break;
                begin = end + 1;
            }

            return modes;
        }
    }

    return {};
}

std::string environment_value(
    const char *name)
{
    const auto value = std::getenv(name);
    return value ? value : "";
}

bool presentation_mode_available(
    PresentationMode mode)
{
    if (mode == PresentationMode::native)
        return true;

    const auto session_type = normalized(
        environment_value("XDG_SESSION_TYPE"));

    return (session_type == "wayland" ||
            !environment_value("WAYLAND_DISPLAY").empty()) &&
           !environment_value("DISPLAY").empty();
}

}

const char *presentation_mode_name(
    PresentationMode mode)
{
    return mode == PresentationMode::xwayland
               ? "xwayland"
               : "native";
}

std::optional<PresentationMode>
parse_presentation_mode(
    const std::string &value)
{
    const auto mode = normalized(value);
    if (mode == "native")
        return PresentationMode::native;
    if (mode == "xwayland")
        return PresentationMode::xwayland;
    return std::nullopt;
}

bool take_presentation_option(
    int &argc,
    char *argv[],
    std::optional<PresentationMode>
        &requested_mode,
    std::string &error)
{
    int write_index = 1;

    for (int read_index = 1;
         read_index < argc;
         ++read_index)
    {
        const std::string argument =
            argv[read_index];

        if (argument ==
            "--xwayland-presentation")
        {
            requested_mode =
                PresentationMode::xwayland;
            continue;
        }

        constexpr char prefix[] =
            "--presentation=";
        if (argument.compare(
                0,
                sizeof(prefix) - 1,
                prefix) == 0)
        {
            requested_mode =
                parse_presentation_mode(
                    argument.substr(
                        sizeof(prefix) - 1));
            if (!requested_mode)
            {
                error =
                    "invalid presentation mode; use native or xwayland";
                return false;
            }
            continue;
        }

        argv[write_index++] =
            argv[read_index];
    }

    argc = write_index;
    argv[argc] = nullptr;
    return true;
}

PresentationSelection select_presentation(
    const std::optional<PresentationMode>
        &requested_mode,
    const std::string &configuration_path)
{
    if (requested_mode)
    {
        return {
            *requested_mode,
            "command line"};
    }

    const auto path =
        configuration_path.empty()
            ? presentation_configuration_path()
            : configuration_path;
    const auto configured_modes =
        read_configured_modes(path);

    for (const auto mode : configured_modes)
    {
        if (presentation_mode_available(mode))
            return {mode, "configuration"};
    }

    // Preserve the useful startup error for a configuration containing only
    // modes that are unavailable in the current session.
    if (!configured_modes.empty())
        return {configured_modes.front(), "configuration"};

    return {};
}

bool prepare_presentation(
    const PresentationSelection &selection,
    std::string &error)
{
    if (selection.mode ==
        PresentationMode::native)
    {
        g_unsetenv(
            "DOCKLIGHT_XWAYLAND_PRESENTATION");
        return true;
    }

    const auto session_type =
        normalized(
            environment_value(
                "XDG_SESSION_TYPE"));
    const auto wayland_display =
        environment_value(
            "WAYLAND_DISPLAY");
    const auto x11_display =
        environment_value("DISPLAY");

    if (session_type != "wayland" &&
        wayland_display.empty())
    {
        error =
            "XWayland presentation requires a Wayland session";
        return false;
    }

    if (x11_display.empty())
    {
        error =
            "XWayland presentation requires DISPLAY from XWayland";
        return false;
    }

    g_setenv("GDK_BACKEND", "x11", true);
    g_setenv(
        "DOCKLIGHT_XWAYLAND_PRESENTATION",
        "1",
        true);
    return true;
}

std::string presentation_configuration_path()
{
    return std::string(
               g_get_user_config_dir()) +
           "/docklight6/presentation.conf";
}

const char *actual_presentation_backend_name()
{
    auto *display =
        gdk_display_get_default();

    if (display && GDK_IS_X11_DISPLAY(display))
        return "X11/XWayland";
    if (display && GDK_IS_WAYLAND_DISPLAY(display))
        return "Wayland";
    return "unknown";
}
