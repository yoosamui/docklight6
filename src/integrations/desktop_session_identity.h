// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// desktop_session_identity.h
//
// Purpose:
// Declares shared, header-only desktop-session identity helpers.
//
// Responsibilities:
// - Normalize desktop identity strings.
// - Distinguish GNOME Shell from GNOME Flashback.
// - Detect Wayland and GNOME Shell X11 sessions from the environment.
//
// Dependencies and ownership:
// Helpers read process environment values and return owned strings or plain
// booleans; they retain no environment pointers or session resources.
//
// Design notes:
// Session identity is shared by presentation and window-backend selection so
// GNOME Flashback is not mistaken for GNOME Shell.
//
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

inline bool identifies_hyprland(
    const std::string &desktop)
{
    return normalized(desktop).find("hyprland") !=
           std::string::npos;
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

inline bool is_hyprland_wayland_session()
{
    auto desktop = environment_value(
        "XDG_CURRENT_DESKTOP");
    if (desktop.empty())
    {
        desktop = environment_value(
            "XDG_SESSION_DESKTOP");
    }

    return is_wayland_session() &&
           identifies_hyprland(desktop);
}

inline bool is_gnome_shell_x11_session()
{
    const auto session_type = normalized(
        environment_value("XDG_SESSION_TYPE"));
    if (session_type != "x11")
        return false;

    auto desktop = environment_value(
        "XDG_CURRENT_DESKTOP");
    if (desktop.empty())
    {
        desktop = environment_value(
            "XDG_SESSION_DESKTOP");
    }

    return identifies_gnome_shell(desktop);
}

}
