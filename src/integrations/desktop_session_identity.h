// ------------------------------------------------------------
// Docklight 6.0
//
// Shared desktop-session identity helpers.
// ------------------------------------------------------------

#pragma once

#include <algorithm>
#include <cctype>
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

}
