#include "gnome_wayland_window_backend.h"

std::string
GnomeWaylandWindowBackend::name() const
{
    return "GNOME Wayland";
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
    capabilities.provides_dock_reveal_trigger = true;

    return capabilities;
}
