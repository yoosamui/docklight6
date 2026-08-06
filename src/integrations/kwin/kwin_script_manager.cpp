// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// kwin_script_manager.cpp
//
// Implementation overview:
// Locates the installed Docklight KWin script and asks KWin's scripting
// service to unload, load, and start it.
//
// Important implementation decisions:
// - Candidate paths support installed and development layouts.
// - Script lifecycle is controlled through KWin's public D-Bus API.
// - Failures are reported without retaining partial manager state.
//
// ------------------------------------------------------------

#include "kwin_script_manager.h"

#include <gio/gio.h>
#include <glib.h>

#include <string>

namespace
{

constexpr char KWIN_SERVICE[] =
    "org.kde.KWin"; // KWin D-Bus service
constexpr char SCRIPTING_OBJECT_PATH[] =
    "/Scripting"; // KWin scripting D-Bus object path
constexpr char SCRIPTING_INTERFACE[] =
    "org.kde.kwin.Scripting"; // KWin scripting D-Bus interface
constexpr char SCRIPT_ID[] =
    "org.docklight6.windowintegration"; // Installed DockLight KWin script ID
constexpr char SCRIPT_RELATIVE_PATH[] =
    "kwin/scripts/"
    "org.docklight6.windowintegration/"
    "contents/code/main.js"; // KWin script path below a data directory
constexpr int DBUS_TIMEOUT_MILLISECONDS =
    2000; // KWin D-Bus call timeout

GVariant *call_scripting_method(
    GDBusConnection *connection,
    const char *method,
    GVariant *parameters,
    const GVariantType *reply_type,
    GError **error)
{
    return g_dbus_connection_call_sync(
        connection,
        KWIN_SERVICE,
        SCRIPTING_OBJECT_PATH,
        SCRIPTING_INTERFACE,
        method,
        parameters,
        reply_type,
        G_DBUS_CALL_FLAGS_NONE,
        DBUS_TIMEOUT_MILLISECONDS,
        nullptr,
        error);
}

std::string script_path_in(
    const char *data_directory)
{
    if (!data_directory ||
        data_directory[0] == '\0')
    {
        return {};
    }

    auto path =
        g_build_filename(
            data_directory,
            SCRIPT_RELATIVE_PATH,
            nullptr);

    std::string result;

    if (g_file_test(
            path,
            G_FILE_TEST_IS_REGULAR))
    {
        result = path;
    }

    g_free(path);

    return result;
}

}

bool KWinScriptManager::restart()
{
    const auto script_path =
        installed_script_path();

    if (script_path.empty())
    {
        g_warning(
            "Docklight KWin integration script is not installed; run 'make install-kwin-integration'");
        return false;
    }

    GError *error = nullptr;

    auto connection =
        g_bus_get_sync(
            G_BUS_TYPE_SESSION,
            nullptr,
            &error);

    if (!connection)
    {
        g_warning(
            "Cannot connect to KWin scripting: %s",
            error
                ? error->message
                : "unknown error");

        g_clear_error(&error);
        return false;
    }

    auto result =
        call_scripting_method(
            connection,
            "isScriptLoaded",
            g_variant_new(
                "(s)",
                SCRIPT_ID),
            G_VARIANT_TYPE("(b)"),
            &error);

    gboolean loaded = false;

    if (result)
    {
        g_variant_get(
            result,
            "(b)",
            &loaded);
        g_variant_unref(result);
    }

    if (!result)
    {
        g_warning(
            "Cannot query the Docklight KWin script: %s",
            error
                ? error->message
                : "unknown error");

        g_clear_error(&error);
        g_object_unref(connection);
        return false;
    }

    if (loaded)
    {
        result =
            call_scripting_method(
                connection,
                "unloadScript",
                g_variant_new(
                    "(s)",
                    SCRIPT_ID),
                G_VARIANT_TYPE("(b)"),
                &error);

        gboolean unloaded = false;

        if (result)
        {
            g_variant_get(
                result,
                "(b)",
                &unloaded);
            g_variant_unref(result);
        }

        if (!result ||
            !unloaded)
        {
            g_warning(
                "Cannot unload the previous Docklight KWin script instance: %s",
                error
                    ? error->message
                    : "KWin rejected the request");

            g_clear_error(&error);
            g_object_unref(connection);
            return false;
        }
    }

    result =
        call_scripting_method(
            connection,
            "loadScript",
            g_variant_new(
                "(ss)",
                script_path.c_str(),
                SCRIPT_ID),
            G_VARIANT_TYPE("(i)"),
            &error);

    gint script_number = -1;

    if (result)
    {
        g_variant_get(
            result,
            "(i)",
            &script_number);
        g_variant_unref(result);
    }

    if (!result ||
        script_number < 0)
    {
        g_warning(
            "Cannot load the Docklight KWin script: %s",
            error
                ? error->message
                : "KWin rejected the request");

        g_clear_error(&error);
        g_object_unref(connection);
        return false;
    }

    result =
        call_scripting_method(
            connection,
            "start",
            nullptr,
            G_VARIANT_TYPE("()"),
            &error);

    if (!result)
    {
        g_warning(
            "Cannot start the Docklight KWin script: %s",
            error
                ? error->message
                : "unknown error");

        g_clear_error(&error);
        g_object_unref(connection);
        return false;
    }

    g_variant_unref(result);
    g_object_unref(connection);

    g_message(
        "Docklight KWin integration script restarted");

    return true;
}

std::string
KWinScriptManager::installed_script_path()
{
    auto path =
        script_path_in(
            g_get_user_data_dir());

    if (!path.empty())
        return path;

    const auto data_directories =
        g_get_system_data_dirs();

    for (int index = 0;
         data_directories[index];
         ++index)
    {
        path =
            script_path_in(
                data_directories[index]);

        if (!path.empty())
            return path;
    }

    return {};
}
