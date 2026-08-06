// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// window_system_controller.cpp
//
// Implementation overview:
// Detects KDE Wayland sessions and coordinates the KWin backend,
// registry, D-Bus service, script, and Plasma geometry bridge.
//
// Important implementation decisions:
// - Unsupported sessions keep the dock usable without window control.
// - Owned components are created and destroyed in dependency order.
// - Companion integrations are ensured only after backend connection.
// - Availability reflects a live registry rather than environment alone.
//
// ------------------------------------------------------------

#include "window_system_controller.h"

#include "integrations/kwin/kwin_integration_service.h"
#include "integrations/kwin/kwin_script_manager.h"
#include "integrations/kwin/kwin_window_backend.h"
#include "integrations/plasma/plasma_geometry_bridge_manager.h"
#include "windowing/window_registry.h"

#include <glib.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace
{

std::string environment_value(
    const char *name)
{
    const auto value =
        std::getenv(name);

    return value
               ? value
               : "";
}

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

bool identifies_kde(
    const std::string &desktop)
{
    const auto normalized =
        lowercase(desktop);

    return normalized.find("kde") !=
               std::string::npos ||
           normalized.find("plasma") !=
               std::string::npos;
}

}

WindowSystemController::
    WindowSystemController() = default;

WindowSystemController::
    ~WindowSystemController()
{
    stop();
}

void WindowSystemController::start()
{
    if (m_started)
        return;

    m_started = true;

    if (!is_kde_wayland_session())
    {
        g_message(
            "Window integration is not enabled for this desktop session");
        return;
    }

    m_kwin_backend =
        std::make_unique<
            KWinWindowBackend>();
    m_registry =
        std::make_unique<
            WindowRegistry>(
            *m_kwin_backend);
    m_kwin_service =
        std::make_unique<
            KWinIntegrationService>(
            *m_kwin_backend);

    m_connection_changed =
        m_registry
            ->signal_connection_changed()
            .connect(
                sigc::mem_fun(
                    *this,
                    &WindowSystemController::
                        on_connection_changed));

    m_registry->start();

    if (!m_kwin_service->start())
    {
        g_warning(
            "KWin window integration could not be started");

        stop();
        return;
    }

    g_message(
        "KWin window integration is ready for the KWin script");

    PlasmaGeometryBridgeManager
        bridge_manager;
    bridge_manager.ensure();

    KWinScriptManager script_manager;
    script_manager.restart();
}

void WindowSystemController::stop()
{
    if (!m_started)
        return;

    if (m_kwin_service)
        m_kwin_service->stop();

    if (m_registry)
        m_registry->stop();

    m_connection_changed.disconnect();

    m_kwin_service.reset();
    m_registry.reset();
    m_kwin_backend.reset();

    m_started = false;
}

bool WindowSystemController::available() const
{
    return m_kwin_service &&
           m_kwin_service->available();
}

WindowRegistry *
WindowSystemController::registry()
{
    return m_registry.get();
}

const WindowRegistry *
WindowSystemController::registry() const
{
    return m_registry.get();
}

bool WindowSystemController::
    is_kde_wayland_session()
{
    const auto session_type =
        lowercase(
            environment_value(
                "XDG_SESSION_TYPE"));

    const bool wayland =
        session_type == "wayland" ||
        !environment_value(
             "WAYLAND_DISPLAY")
             .empty();

    const bool kde =
        identifies_kde(
            environment_value(
                "XDG_CURRENT_DESKTOP")) ||
        identifies_kde(
            environment_value(
                "XDG_SESSION_DESKTOP")) ||
        lowercase(
            environment_value(
                "KDE_FULL_SESSION")) ==
            "true";

    return wayland && kde;
}

void WindowSystemController::
    on_connection_changed(
        bool connected)
{
    g_message(
        "KWin window integration %s",
        connected
            ? "connected"
            : "disconnected");
}
