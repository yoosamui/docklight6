// ------------------------------------------------------------
// Docklight 6.0
//
// GNOME Shell/Mutter X11 hybrid backend.
// ------------------------------------------------------------

#pragma once

#include "integrations/gnome/gnome_wayland_window_backend.h"
#include "integrations/x11/mutter_window_backend.h"

#include <sigc++/connection.h>

#include <memory>
#include <vector>

class KWinIntegrationService;

// Application windows remain entirely owned by MutterWindowBackend's mature
// EWMH/XComposite implementation.  The private Shell backend carries only
// DockLight surface geometry and autohide animation messages.
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
