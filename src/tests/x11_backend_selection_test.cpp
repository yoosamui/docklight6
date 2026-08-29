// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// x11_backend_selection_test.cpp
//
// Purpose:
// Verifies pure X11 backend and desktop-file identity selection.
//
// Responsibilities:
// - Check window-manager-name precedence over desktop metadata.
// - Check desktop fallbacks when the manager name is unavailable.
// - Check GNOME Flashback identity and LibreOffice launcher resolution.
//
// Dependencies and ownership:
// The test uses only value-returning classification helpers and owns no
// desktop or display-server resources.
//
// Design notes:
// Unknown explicit window managers must remain on the generic EWMH path.
//
// ------------------------------------------------------------

#include "integrations/x11/x11_backend_selection.h"
#include "integrations/desktop_session_identity.h"

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

bool expect_desktop_file_name(
    const std::string &window_class,
    const std::string &caption,
    const std::string &expected)
{
    const auto actual =
        resolve_x11_desktop_file_name(
            window_class,
            caption);
    if (actual == expected)
        return true;

    std::cerr
        << "desktop-file mismatch for class '"
        << window_class
        << "' and caption '"
        << caption
        << "': expected "
        << expected
        << ", got "
        << actual
        << '\n';
    return false;
}

}

int main()
{
    bool passed = true;

    passed &= DesktopSessionIdentity::
        identifies_gnome_shell("GNOME");
    passed &= !DesktopSessionIdentity::
        identifies_gnome_shell(
            "GNOME-Flashback:GNOME:");
    passed &= DesktopSessionIdentity::
        identifies_gnome_flashback(
            "gnome-flashback-metacity");

    passed &= expect_backend(
        "KWin",
        "KDE",
        X11BackendKind::kwin);
    passed &= expect_backend(
        "kwin_x11",
        "KDE",
        X11BackendKind::kwin);
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
        "XFCE",
        X11BackendKind::marco);
    passed &= expect_backend(
        "Metacity",
        "",
        X11BackendKind::marco);
    passed &= expect_backend(
        "Metacity",
        "GNOME-Flashback:GNOME:",
        X11BackendKind::marco);
    passed &= expect_backend(
        "Openbox",
        "XFCE",
        X11BackendKind::openbox);
    passed &= expect_backend(
        "i3",
        "LXDE",
        X11BackendKind::ewmh_fallback);

    // Desktop metadata is a startup fallback only when EWMH has not yet
    // exposed the actual manager name.
    passed &= expect_backend(
        "",
        "KDE",
        X11BackendKind::kwin);
    passed &= expect_backend(
        "",
        "KDE:Plasma",
        X11BackendKind::kwin);
    passed &= expect_backend(
        "",
        "MATE",
        X11BackendKind::marco);
    passed &= expect_backend(
        "",
        "GNOME-Flashback:GNOME:",
        X11BackendKind::marco);
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
        "LXDE",
        X11BackendKind::openbox);
    passed &= expect_backend(
        "",
        "LXQt",
        X11BackendKind::openbox);

    passed &= expect_desktop_file_name(
        "soffice.bin",
        "report.odt — LibreOffice Writer",
        "libreoffice-writer");
    passed &= expect_desktop_file_name(
        "soffice.bin",
        "budget.ods — LibreOffice Calc",
        "libreoffice-calc");
    passed &= expect_desktop_file_name(
        "Navigator",
        "Mozilla Firefox",
        "Navigator");

    return passed ? 0 : 1;
}
