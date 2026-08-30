// ------------------------------------------------------------
// Docklight 6.0
//
// Parses Hyprland edge-gap settings and derives the layer-shell exclusive
// zone used by the XWayland reservation companion.
// ------------------------------------------------------------

#pragma once

#include <string>

int hyprland_outer_gap_from_option_json(
    const std::string &json,
    const std::string &edge);

int hyprland_reservation_size(
    int dock_size,
    int outer_gap);
