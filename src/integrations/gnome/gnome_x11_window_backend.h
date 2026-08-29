// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// gnome_x11_window_backend.h
//
// Purpose:
// Declares the GNOME Shell/Mutter X11 hybrid window backend.
//
// Responsibilities:
// - Retain Mutter EWMH/XComposite application-window behavior.
// - Host the optional GNOME Shell surface-animation bridge.
// - Mirror dock placement and hidden state into the Shell transport.
// - Forward Shell surface geometry and animation completion signals.
//
// Dependencies and ownership:
// The backend inherits the Mutter X11 backend and owns its private Shell
// backend, D-Bus service, and bridge signal connections.
//
// Design notes:
// Application windows remain authoritative in the X11 backend; the Shell
// bridge carries only Docklight surface state and animation messages.
//
// ------------------------------------------------------------

#pragma once

#include "integrations/gnome/gnome_wayland_window_backend.h"
#include "integrations/x11/mutter_window_backend.h"

#include <sigc++/connection.h>

#include <memory>
#include <vector>

class KWinIntegrationService;

class GnomeX11WindowBackend final : public MutterWindowBackend
{
public:
    GnomeX11WindowBackend();
    ~GnomeX11WindowBackend() override;

    void start() override;
    void stop() override;

    std::string name() const override;
    WindowBackendCapabilities capabilities() const override;
    std::optional<WindowIconGeometry>
    dock_surface_geometry() const override;

private:
    void connect_shell_bridge();
    void disconnect_shell_bridge();

private:
    GnomeWaylandWindowBackend m_shell_backend;
    std::unique_ptr<KWinIntegrationService> m_shell_service;
    std::vector<sigc::connection> m_shell_connections;
    bool m_started = false;
    bool m_shell_service_available = false;
};
