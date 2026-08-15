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

    // KWin on X11 exposes ordinary EWMH/XIDs. Its dedicated backend keeps
    // the native X11 capture path instead of the Wayland script protocol.
    if (contains(manager, "kwin"))
        return X11BackendKind::kwin;
    if (contains(manager, "marco") ||
        contains(manager, "metacity"))
    {
        return X11BackendKind::marco;
    }
    if (contains(manager, "muffin"))
        return X11BackendKind::muffin;
    if (contains(manager, "mutter") ||
        contains(manager, "gnome shell"))
    {
        return X11BackendKind::mutter;
    }
    if (contains(manager, "openbox"))
        return X11BackendKind::openbox;
    if (contains(manager, "xfwm"))
        return X11BackendKind::xfwm4;

    // Environment fallback is used only when EWMH has not exposed the WM
    // name yet. An explicit but unknown manager always uses the generic path.
    if (!manager.empty())
        return X11BackendKind::ewmh_fallback;

    const auto desktop =
        lowercase(desktop_name);
    if (contains(desktop, "kde") ||
        contains(desktop, "plasma"))
    {
        return X11BackendKind::kwin;
    }
    if (contains(desktop, "mate"))
        return X11BackendKind::marco;
    if (contains(desktop, "cinnamon"))
        return X11BackendKind::muffin;
    if (contains(desktop, "gnome"))
        return X11BackendKind::mutter;
    if (contains(desktop, "lxde") ||
        contains(desktop, "lxqt"))
    {
        return X11BackendKind::openbox;
    }
    if (contains(desktop, "xfce"))
        return X11BackendKind::xfwm4;

    return X11BackendKind::ewmh_fallback;
}

const char *x11_backend_kind_name(
    X11BackendKind kind)
{
    switch (kind)
    {
    case X11BackendKind::kwin:
        return "KWin/X11";
    case X11BackendKind::marco:
        return "Marco/Metacity";
    case X11BackendKind::muffin:
        return "Muffin";
    case X11BackendKind::mutter:
        return "Mutter";
    case X11BackendKind::openbox:
        return "Openbox";
    case X11BackendKind::xfwm4:
        return "Xfwm4";
    case X11BackendKind::ewmh_fallback:
        return "EWMH fallback";
    }

    return "EWMH fallback";
}

std::string resolve_x11_desktop_file_name(
    const std::string &class_group_name,
    const std::string &window_caption)
{
    const auto window_class =
        lowercase(class_group_name);

    // LibreOffice exposes the same generic soffice.bin WM_CLASS for every
    // module on X11. Its window title is the only module-specific EWMH value,
    // so use it to select the installed launcher and matching themed icon.
    if (window_class == "soffice.bin" ||
        window_class == "soffice" ||
        window_class == "libreoffice")
    {
        const auto caption =
            lowercase(window_caption);
        constexpr const char *modules[] = {
            "writer",
            "calc",
            "impress",
            "draw",
            "math",
            "base"};

        for (const auto *module : modules)
        {
            const auto module_title =
                std::string{"libreoffice "} +
                module;
            if (caption.find(module_title) !=
                std::string::npos)
            {
                return std::string{"libreoffice-"} +
                       module;
            }
        }
    }

    return class_group_name;
}
