// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// presentation_selector_test.cpp
//
// Purpose:
// Verifies presentation parsing, precedence, persistence, and preparation.
//
// Responsibilities:
// - Check command-line extraction and invalid option handling.
// - Check configuration and automatic desktop selection.
// - Check XWayland availability and launch-context environment behavior.
//
// Dependencies and ownership:
// Tests use temporary configuration directories and process environment
// variables, restoring or removing their local fixtures before exit.
//
// Design notes:
// Selection tests avoid opening a display so transport policy stays testable
// independently from GTK initialization.
//
// ------------------------------------------------------------

#include "presentation/presentation_selector.h"

#include <gio/gio.h>
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
    assert(parse_presentation_mode("auto") ==
           PresentationMode::automatic);

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

void verifies_automatic_desktop_selection()
{
    GError *error = nullptr;
    auto *temporary_directory =
        g_dir_make_tmp(
            "docklight-presentation-auto-XXXXXX",
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
        output << "mode=auto\n";
        assert(output);
    }

    g_setenv("XDG_SESSION_TYPE", "wayland", true);
    g_setenv("WAYLAND_DISPLAY", "wayland-test", true);
    g_setenv("DISPLAY", ":99", true);
    g_setenv("XDG_CURRENT_DESKTOP", "GNOME", true);
    g_unsetenv("XDG_SESSION_DESKTOP");

    auto selection = select_presentation(
        std::nullopt,
        path);
    assert(selection.mode ==
           PresentationMode::xwayland);
    assert(selection.source ==
           "configuration auto");

    g_setenv("XDG_CURRENT_DESKTOP", "KDE", true);
    g_setenv("XDG_SESSION_DESKTOP", "GNOME", true);
    selection = select_presentation(
        std::nullopt,
        path);
    assert(selection.mode ==
           PresentationMode::native);

    g_setenv("XDG_CURRENT_DESKTOP", "GNOME", true);
    g_unsetenv("DISPLAY");
    selection = select_presentation(
        std::nullopt,
        path);
    assert(selection.mode ==
           PresentationMode::native);

    g_setenv("XDG_SESSION_TYPE", "x11", true);
    g_setenv("DISPLAY", ":99", true);
    selection = select_presentation(
        std::nullopt,
        path);
    assert(selection.mode ==
           PresentationMode::native);

    g_setenv("XDG_SESSION_TYPE", "wayland", true);
    g_setenv("XDG_CURRENT_DESKTOP", "KDE", true);
    selection = select_presentation(
        PresentationMode::xwayland,
        path);
    assert(selection.mode ==
           PresentationMode::xwayland);

    g_remove(path.c_str());

    g_setenv("XDG_CURRENT_DESKTOP", "GNOME", true);
    selection = select_presentation(
        std::nullopt,
        path);
    assert(selection.mode ==
           PresentationMode::xwayland);
    assert(selection.source ==
           "default auto");

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

void verifies_application_launch_environment()
{
    g_setenv("GDK_BACKEND", "x11", true);
    g_setenv(
        "DOCKLIGHT_XWAYLAND_PRESENTATION",
        "1",
        true);
    g_setenv(
        "DOCKLIGHT_LAUNCH_TEST_VALUE",
        "preserved",
        true);

    auto *context =
        g_app_launch_context_new();
    prepare_application_launch_context(
        context);

    auto *environment =
        g_app_launch_context_get_environment(
            context);
    assert(!g_environ_getenv(
        environment,
        "GDK_BACKEND"));
    assert(!g_environ_getenv(
        environment,
        "DOCKLIGHT_XWAYLAND_PRESENTATION"));
    assert(std::string(g_environ_getenv(
               environment,
               "DOCKLIGHT_LAUNCH_TEST_VALUE")) ==
           "preserved");

    g_strfreev(environment);
    g_object_unref(context);

    // A native presentation must preserve an explicit backend selection
    // supplied by the user's session rather than treating it as Docklight's
    // private XWayland override.
    g_unsetenv(
        "DOCKLIGHT_XWAYLAND_PRESENTATION");
    g_setenv("GDK_BACKEND", "wayland", true);

    context = g_app_launch_context_new();
    prepare_application_launch_context(
        context);
    environment =
        g_app_launch_context_get_environment(
            context);
    assert(std::string(g_environ_getenv(
               environment,
               "GDK_BACKEND")) ==
           "wayland");

    g_strfreev(environment);
    g_object_unref(context);
    g_unsetenv(
        "DOCKLIGHT_LAUNCH_TEST_VALUE");
}

}

int main()
{
    verifies_command_line_selection();
    verifies_configuration_and_precedence();
    verifies_automatic_desktop_selection();
    verifies_xwayland_environment();
    verifies_application_launch_environment();
    return 0;
}
