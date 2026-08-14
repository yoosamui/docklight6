// ------------------------------------------------------------
// Docklight 6.0
//
// Verifies command-line precedence, persistent presentation selection, and
// XWayland environment validation without opening a display.
// ------------------------------------------------------------

#include "presentation/presentation_selector.h"

#include <glib.h>
#include <glib/gstdio.h>

#include <cassert>
#include <fstream>
#include <optional>
#include <string>

namespace
{

void verifies_command_line_selection()
{
    char program[] = "docklight6";
    char option[] = "--presentation=xwayland";
    char retained[] = "--retained";
    char *arguments[] = {
        program,
        option,
        retained,
        nullptr};
    int count = 3;
    std::optional<PresentationMode> mode;
    std::string error;

    assert(take_presentation_option(
        count,
        arguments,
        mode,
        error));
    assert(mode == PresentationMode::xwayland);
    assert(count == 2);
    assert(std::string(arguments[1]) ==
           "--retained");

    char invalid[] = "--presentation=invalid";
    char *invalid_arguments[] = {
        program,
        invalid,
        nullptr};
    count = 2;
    mode.reset();
    error.clear();
    assert(!take_presentation_option(
        count,
        invalid_arguments,
        mode,
        error));
    assert(!error.empty());
}

void verifies_configuration_and_precedence()
{
    GError *error = nullptr;
    auto *temporary_directory =
        g_dir_make_tmp(
            "docklight-presentation-XXXXXX",
            &error);
    assert(temporary_directory);
    assert(!error);

    const std::string directory =
        temporary_directory;
    g_free(temporary_directory);
    const auto path =
        directory + "/presentation.conf";

    {
        std::ofstream output(path);
        output << "mode=xwayland\n";
        assert(output);
    }

    auto selection =
        select_presentation(
            std::nullopt,
            path);
    assert(selection.mode ==
           PresentationMode::xwayland);
    assert(selection.source ==
           "configuration");

    selection = select_presentation(
        PresentationMode::native,
        path);
    assert(selection.mode ==
           PresentationMode::native);
    assert(selection.source ==
           "command line");

    {
        std::ofstream output(path);
        output << "mode=xwayland,native\n";
        assert(output);
    }

    g_setenv("XDG_SESSION_TYPE", "x11", true);
    g_unsetenv("WAYLAND_DISPLAY");
    g_setenv("DISPLAY", ":99", true);
    selection = select_presentation(
        std::nullopt,
        path);
    assert(selection.mode ==
           PresentationMode::native);

    g_setenv("XDG_SESSION_TYPE", "wayland", true);
    g_setenv("WAYLAND_DISPLAY", "wayland-test", true);
    selection = select_presentation(
        std::nullopt,
        path);
    assert(selection.mode ==
           PresentationMode::xwayland);

    g_remove(path.c_str());
    g_rmdir(directory.c_str());
}

void verifies_xwayland_environment()
{
    g_setenv(
        "XDG_SESSION_TYPE",
        "wayland",
        true);
    g_setenv(
        "WAYLAND_DISPLAY",
        "wayland-test",
        true);
    g_setenv("DISPLAY", ":99", true);

    std::string error;
    assert(prepare_presentation(
        {PresentationMode::xwayland,
         "test"},
        error));
    assert(std::string(g_getenv(
               "GDK_BACKEND")) == "x11");
    assert(std::string(g_getenv(
               "DOCKLIGHT_XWAYLAND_PRESENTATION")) ==
           "1");

    g_unsetenv("DISPLAY");
    error.clear();
    assert(!prepare_presentation(
        {PresentationMode::xwayland,
         "test"},
        error));
    assert(!error.empty());
}

}

int main()
{
    verifies_command_line_selection();
    verifies_configuration_and_precedence();
    verifies_xwayland_environment();
    return 0;
}
