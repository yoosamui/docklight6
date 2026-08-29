// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// gnome_x11_window_backend.cpp
//
// Implementation overview:
// Combines Mutter's X11 window model with an optional GNOME Shell bridge for
// Docklight surface geometry and autohide animation.
//
// Important implementation decisions:
// - The ordinary X11 backend starts first and remains authoritative.
// - Shell bridge availability gates only Shell-provided capabilities.
// - Pre-service dock state is replayed after D-Bus startup.
// - Every bridge signal is disconnected before either backend stops.
//
// ------------------------------------------------------------

#include "gnome_x11_window_backend.h"

#include "integrations/kwin/kwin_integration_service.h"

GnomeX11WindowBackend::GnomeX11WindowBackend() = default;

GnomeX11WindowBackend::~GnomeX11WindowBackend()
{
    stop();
}

void GnomeX11WindowBackend::start()
{
    if (m_started)
        return;

    m_started = true;

    // Start the ordinary X11 backend first.  It remains authoritative even
    // when the optional Shell bridge is absent or refuses registration.
    MutterWindowBackend::start();

    m_shell_backend.start();
    connect_shell_bridge();
    m_shell_service =
        std::make_unique<KWinIntegrationService>(
            m_shell_backend);
    m_shell_service_available =
        m_shell_service->start();

    // Mirror any layout state produced before the D-Bus object became ready.
    m_shell_backend.set_dock_placement_geometry(
        dock_placement_geometry());
    m_shell_backend.set_dock_hidden(
        dock_hidden());
}

void GnomeX11WindowBackend::stop()
{
    if (!m_started)
        return;

    m_shell_service_available = false;
    if (m_shell_service)
        m_shell_service->stop();
    m_shell_service.reset();
    disconnect_shell_bridge();
    m_shell_backend.stop();
    MutterWindowBackend::stop();
    m_started = false;
}

std::string GnomeX11WindowBackend::name() const
{
    return "GNOME Shell/Mutter X11";
}

WindowBackendCapabilities
GnomeX11WindowBackend::capabilities() const
{
    auto capabilities =
        MutterWindowBackend::capabilities();
    capabilities.provides_dock_autohide_animation =
        m_shell_service_available;
    return capabilities;
}

std::optional<WindowIconGeometry>
GnomeX11WindowBackend::dock_surface_geometry() const
{
    if (!m_shell_service_available)
        return std::nullopt;

    return m_shell_backend.dock_surface_geometry();
}

void GnomeX11WindowBackend::connect_shell_bridge()
{
    m_shell_connections.push_back(
        signal_dock_placement_geometry_changed()
            .connect(
                [this]()
                {
                    m_shell_backend
                        .set_dock_placement_geometry(
                            dock_placement_geometry());
                }));
    m_shell_connections.push_back(
        signal_dock_hidden_changed()
            .connect(
                [this](bool hidden)
                {
                    m_shell_backend.set_dock_hidden(
                        hidden);
                }));
    m_shell_connections.push_back(
        m_shell_backend
            .signal_dock_surface_geometry_changed()
            .connect(
                [this]()
                {
                    notify_dock_surface_geometry_changed();
                }));
    m_shell_connections.push_back(
        m_shell_backend
            .signal_dock_animation_completed()
            .connect(
                [this](bool hidden)
                {
                    signal_dock_animation_completed()
                        .emit(hidden);
                }));
}

void GnomeX11WindowBackend::disconnect_shell_bridge()
{
    for (auto &connection : m_shell_connections)
        connection.disconnect();
    m_shell_connections.clear();
}
