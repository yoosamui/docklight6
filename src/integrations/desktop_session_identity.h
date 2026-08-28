// ------------------------------------------------------------
// Docklight 6.0
//
// Shared desktop-session identity helpers.
// ------------------------------------------------------------

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace DesktopSessionIdentity
{

inline std::string normalized(
    std::string value)
{
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

inline bool identifies_gnome(
    const std::string &desktop)
{
    return normalized(desktop).find("gnome") !=
           std::string::npos;
}

inline bool identifies_gnome_flashback(
    const std::string &desktop)
{
    const auto identity =
        normalized(desktop);

    return identity.find("gnome-flashback") !=
               std::string::npos ||
           identity.find("gnome flashback") !=
               std::string::npos;
}

inline bool identifies_gnome_shell(
    const std::string &desktop)
{
    return identifies_gnome(desktop) &&
           !identifies_gnome_flashback(desktop);
}

inline std::string environment_value(
    const char *name)
{
    const auto *value = std::getenv(name);
    return value ? value : "";
}

inline bool is_wayland_session()
{
    const auto session_type = normalized(
        environment_value("XDG_SESSION_TYPE"));

    return session_type == "wayland" ||
           (session_type.empty() &&
            !environment_value("WAYLAND_DISPLAY").empty());
}

inline bool is_gnome_wayland_session()
{
    auto desktop = environment_value(
        "XDG_CURRENT_DESKTOP");
    if (desktop.empty())
    {
        desktop = environment_value(
            "XDG_SESSION_DESKTOP");
    }

    return is_wayland_session() &&
           identifies_gnome_shell(desktop);
}

}
