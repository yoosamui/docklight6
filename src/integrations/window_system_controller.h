// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// window_system_controller.h
//
// Purpose:
// Declares lifecycle ownership for Docklight's desktop window integration.
//
// Responsibilities:
// - Detect the active desktop, compositor, and X11 window manager.
// - Select script-backed KWin integration on Wayland/X11, or a WM-specific
//   EWMH integration for other X11 window managers.
// - Construct and start backend, registry, and D-Bus service in order.
// - Provision companion KWin and Plasma components after connection.
// - Tear the integration stack down safely.
//
// Dependencies and ownership:
// The controller uniquely owns its backend, registry, and service.
// Callers receive borrowed registry pointers valid for its lifetime.
//
// Design notes:
// Desktop-specific startup is isolated here so the core dock can run
// without window-management features on unsupported sessions.
//
// ------------------------------------------------------------

#pragma once

#include <sigc++/connection.h>

#include <memory>

class KWinIntegrationService;
class WindowBackend;
class WindowRegistry;

class WindowSystemController
{
public:
    WindowSystemController();
    ~WindowSystemController();

    void start();
    void stop();

    bool available() const;

    WindowRegistry *registry();
    const WindowRegistry *registry() const;

private:
    static bool is_kde_wayland_session();
    static bool is_x11_session();

    void on_connection_changed(
        bool connected);

private:
    std::unique_ptr<WindowBackend>
        m_backend;
    std::unique_ptr<WindowRegistry>
        m_registry;
    std::unique_ptr<KWinIntegrationService>
        m_kwin_service;

    sigc::connection
        m_connection_changed;

    bool m_started = false;
};
