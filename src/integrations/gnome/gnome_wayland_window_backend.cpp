// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// gnome_wayland_window_backend.cpp
//
// Implementation overview:
// Supplies the GNOME Shell backend name and its supported window, geometry,
// preview, and dock-surface capabilities.
//
// Important implementation decisions:
// - Capabilities are declared explicitly rather than inherited wholesale.
// - Activities and KWin-specific minimize effects remain disabled.
// - Snapshot and command mechanics stay in the shared base implementation.
//
// ------------------------------------------------------------

#include "gnome_wayland_window_backend.h"

std::string
GnomeWaylandWindowBackend::name() const
{
    return "GNOME Shell";
}

WindowBackendCapabilities
GnomeWaylandWindowBackend::capabilities() const
{
    WindowBackendCapabilities capabilities;

    capabilities.can_activate = true;
    capabilities.can_raise = true;
    capabilities.can_close = true;
    capabilities.can_minimize = true;
    capabilities.can_maximize = true;
    capabilities.provides_stacking_order = true;
    capabilities.provides_virtual_desktops = true;
    capabilities.provides_frame_geometry = true;
    capabilities.provides_icons = true;
    capabilities.accepts_icon_geometry = true;
    capabilities.provides_dock_autohide_animation = true;
    capabilities.provides_dock_reveal_trigger = true;

    return capabilities;
}
