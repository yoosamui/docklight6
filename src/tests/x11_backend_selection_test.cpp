// ------------------------------------------------------------
// Docklight 6.0
//
// Verifies that each desktop-specific X11 backend remains isolated and that
// unknown EWMH window managers use the fallback implementation.
// ------------------------------------------------------------

#include "integrations/x11/x11_backend_selection.h"

#include <iostream>
#include <string>

namespace
{

bool expect_backend(
    const std::string &manager,
    const std::string &desktop,
    X11BackendKind expected)
{
    const auto actual =
        select_x11_backend_kind(
            manager,
            desktop);
    if (actual == expected)
        return true;

    std::cerr
        << "backend mismatch for WM '"
        << manager
        << "' and desktop '"
        << desktop
        << "': expected "
        << x11_backend_kind_name(expected)
        << ", got "
        << x11_backend_kind_name(actual)
        << '\n';
    return false;
}

}

int main()
{
    bool passed = true;

    passed &= expect_backend(
        "KWin",
        "KDE",
        X11BackendKind::ewmh_fallback);
    passed &= expect_backend(
        "kwin_x11",
        "KDE",
        X11BackendKind::ewmh_fallback);
    passed &= expect_backend(
        "Muffin",
        "X-Cinnamon",
        X11BackendKind::muffin);
    passed &= expect_backend(
        "Mutter",
        "GNOME",
        X11BackendKind::mutter);
    passed &= expect_backend(
        "GNOME Shell",
        "GNOME",
        X11BackendKind::mutter);
    passed &= expect_backend(
        "Xfwm4",
        "XFCE",
        X11BackendKind::xfwm4);
    passed &= expect_backend(
        "Marco",
        "MATE",
        X11BackendKind::ewmh_fallback);
    passed &= expect_backend(
        "Openbox",
        "XFCE",
        X11BackendKind::ewmh_fallback);

    // Desktop metadata is a startup fallback only when EWMH has not yet
    // exposed the actual manager name.
    passed &= expect_backend(
        "",
        "KDE",
        X11BackendKind::ewmh_fallback);
    passed &= expect_backend(
        "",
        "KDE:Plasma",
        X11BackendKind::ewmh_fallback);
    passed &= expect_backend(
        "",
        "Cinnamon",
        X11BackendKind::muffin);
    passed &= expect_backend(
        "",
        "GNOME",
        X11BackendKind::mutter);
    passed &= expect_backend(
        "",
        "XFCE",
        X11BackendKind::xfwm4);
    passed &= expect_backend(
        "",
        "LXQt",
        X11BackendKind::ewmh_fallback);

    return passed ? 0 : 1;
}
