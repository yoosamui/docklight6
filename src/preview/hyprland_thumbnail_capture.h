// ------------------------------------------------------------
// Docklight 6.0
//
// Native Hyprland toplevel capture through the standard Wayland image-copy
// protocols. Hyprland stableId values match ext-foreign-toplevel identifiers.
// ------------------------------------------------------------

#pragma once

#include "windowing/managed_window.h"

#include <optional>
#include <vector>

struct HyprlandThumbnail
{
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

std::optional<HyprlandThumbnail>
capture_hyprland_toplevel(const WindowId &window_id);
