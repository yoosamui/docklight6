// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// plasma_geometry_bridge_manager.cpp
//
// Implementation overview:
// Locates installed Plasma package metadata and runs the packaged
// installation helper when the geometry bridge must be provisioned.
//
// Important implementation decisions:
// - Candidate paths support installed and development executions.
// - Regular-file checks precede launching any helper.
// - The helper owns package mutation; this class only coordinates it.
//
// ------------------------------------------------------------

#include "plasma_geometry_bridge_manager.h"

#include <gio/gio.h>
#include <glib.h>

#include <string>

namespace
{

constexpr char PLASMA_SERVICE[] =
    "org.kde.plasmashell"; // Plasma Shell D-Bus service
constexpr char PLASMA_OBJECT_PATH[] =
    "/PlasmaShell"; // Plasma Shell D-Bus object path
constexpr char PLASMA_INTERFACE[] =
    "org.kde.PlasmaShell"; // Plasma Shell D-Bus interface
constexpr char BRIDGE_METADATA_RELATIVE_PATH[] =
    "plasma/plasmoids/"
    "org.docklight6.geometrybridge/"
    "metadata.json"; // Installed geometry-bridge metadata path
constexpr char ENSURE_SCRIPT_NAME[] =
    "ensure-geometry-bridge.js"; // Geometry-bridge repair script filename
constexpr int DBUS_TIMEOUT_MILLISECONDS =
    3000; // Plasma D-Bus call timeout

std::string file_path_if_regular(
    const std::string &path)
{
    return g_file_test(
               path.c_str(),
               G_FILE_TEST_IS_REGULAR)
               ? path
               : std::string();
}

std::string installed_metadata_path()
{
    auto path =
        g_build_filename(
            g_get_user_data_dir(),
            BRIDGE_METADATA_RELATIVE_PATH,
            nullptr);

    auto result =
        file_path_if_regular(path);

    g_free(path);

    if (!result.empty())
        return result;

    const auto data_directories =
        g_get_system_data_dirs();

    for (int index = 0;
         data_directories[index];
         ++index)
    {
        path =
            g_build_filename(
                data_directories[index],
                BRIDGE_METADATA_RELATIVE_PATH,
                nullptr);

        result =
            file_path_if_regular(path);

        g_free(path);

        if (!result.empty())
            return result;
    }

    return {};
}

std::string ensure_script_path()
{
    auto path =
        g_build_filename(
            DOCKLIGHT_DATA_DIR,
            ENSURE_SCRIPT_NAME,
            nullptr);

    auto result =
        file_path_if_regular(path);

    g_free(path);

    if (!result.empty())
        return result;

    path =
        g_build_filename(
            SOURCE_DIR,
            "..",
            "plasma",
            "geometry-bridge",
            ENSURE_SCRIPT_NAME,
            nullptr);

    result =
        file_path_if_regular(path);

    g_free(path);

    return result;
}

}

bool PlasmaGeometryBridgeManager::ensure()
{
    if (installed_metadata_path().empty())
    {
        g_warning(
            "Docklight Plasma geometry bridge is not installed; run "
            "'make install-plasma-geometry-bridge' for development or "
            "install the system Docklight package");
        return false;
    }

    const auto script_path =
        ensure_script_path();

    if (script_path.empty())
    {
        g_warning(
            "Docklight Plasma bridge repair script is not installed");
        return false;
    }

    gchar *script = nullptr;
    GError *error = nullptr;

    if (!g_file_get_contents(
            script_path.c_str(),
            &script,
            nullptr,
            &error))
    {
        g_warning(
            "Cannot read the Docklight Plasma bridge repair script: %s",
            error
                ? error->message
                : "unknown error");

        g_clear_error(&error);
        return false;
    }

    auto connection =
        g_bus_get_sync(
            G_BUS_TYPE_SESSION,
            nullptr,
            &error);

    if (!connection)
    {
        g_warning(
            "Cannot connect to Plasma Shell: %s",
            error
                ? error->message
                : "unknown error");

        g_clear_error(&error);
        g_free(script);
        return false;
    }

    auto result =
        g_dbus_connection_call_sync(
            connection,
            PLASMA_SERVICE,
            PLASMA_OBJECT_PATH,
            PLASMA_INTERFACE,
            "evaluateScript",
            g_variant_new(
                "(s)",
                script),
            G_VARIANT_TYPE("(s)"),
            G_DBUS_CALL_FLAGS_NONE,
            DBUS_TIMEOUT_MILLISECONDS,
            nullptr,
            &error);

    g_free(script);
    g_object_unref(connection);

    if (!result)
    {
        g_warning(
            "Cannot ensure the Docklight Plasma geometry bridge: %s",
            error
                ? error->message
                : "unknown error");

        g_clear_error(&error);
        return false;
    }

    g_variant_unref(result);

    g_message(
        "Docklight Plasma geometry bridge is installed and active");

    return true;
}
