// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// docklight_surface_identity.h
//
// Purpose:
// Defines the stable semantic identity of each DockLight top-level surface.
//
// Design notes:
// GTK surfaces declare these values without interpreting the desktop. Native
// integrations may use the role or layer-shell namespace to distinguish the
// main dock from auxiliary windows.
//
// ------------------------------------------------------------

#pragma once

namespace DocklightSurfaceIdentity
{
    constexpr char DOCK_ROLE[] =
        "docklight6-dock";
    constexpr char REVEAL_ROLE[] =
        "docklight6-reveal";
    constexpr char TOOLTIP_ROLE[] =
        "docklight6-tooltip";
    constexpr char PREVIEW_ROLE[] =
        "docklight6-preview";
    constexpr char SETTINGS_ROLE[] =
        "docklight6-settings";
    constexpr char ABOUT_ROLE[] =
        "docklight6-about";
    constexpr char ICON_CHOOSER_ROLE[] =
        "docklight6-icon-chooser";
    constexpr char COMPOSITOR_WARNING_ROLE[] =
        "docklight6-compositor-warning";

    constexpr char DOCK_NAMESPACE[] =
        "docklight6";
    constexpr char REVEAL_NAMESPACE[] =
        "docklight6-autohide-reveal";
    constexpr char TOOLTIP_NAMESPACE[] =
        "docklight6-tooltip";
    constexpr char PREVIEW_NAMESPACE[] =
        "docklight6-preview";
    constexpr char ABOUT_NAMESPACE[] =
        "docklight6-about";
}
