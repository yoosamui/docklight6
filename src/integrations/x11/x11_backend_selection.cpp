#include "x11_backend_selection.h"

#include <algorithm>
#include <cctype>

namespace
{

std::string lowercase(
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

bool contains(
    const std::string &value,
    const char *token)
{
    return value.find(token) !=
           std::string::npos;
}

}

X11BackendKind select_x11_backend_kind(
    const std::string &window_manager_name,
    const std::string &desktop_name)
{
    const auto manager =
        lowercase(window_manager_name);

    if (contains(manager, "muffin"))
        return X11BackendKind::muffin;
    if (contains(manager, "mutter") ||
        contains(manager, "gnome shell"))
    {
        return X11BackendKind::mutter;
    }
    if (contains(manager, "xfwm"))
        return X11BackendKind::xfwm4;

    // Environment fallback is used only when EWMH has not exposed the WM
    // name yet. An explicit but unknown manager always uses the generic path.
    if (!manager.empty())
        return X11BackendKind::ewmh_fallback;

    const auto desktop =
        lowercase(desktop_name);
    if (contains(desktop, "cinnamon"))
        return X11BackendKind::muffin;
    if (contains(desktop, "gnome"))
        return X11BackendKind::mutter;
    if (contains(desktop, "xfce"))
        return X11BackendKind::xfwm4;

    return X11BackendKind::ewmh_fallback;
}

const char *x11_backend_kind_name(
    X11BackendKind kind)
{
    switch (kind)
    {
    case X11BackendKind::muffin:
        return "Muffin";
    case X11BackendKind::mutter:
        return "Mutter";
    case X11BackendKind::xfwm4:
        return "Xfwm4";
    case X11BackendKind::ewmh_fallback:
        return "EWMH fallback";
    }

    return "EWMH fallback";
}
