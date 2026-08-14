// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_constants.h
//
// Purpose:
// Declares shared interaction limits, timing defaults, and protocol
// identifiers used by dock components.
//
// Responsibilities:
// - Centralize tooltip and autohide timing.
// - Provide stable constants for dock interaction behavior.
// - Keep runtime layout choices outside compile-time defaults.
//
// Dependencies and ownership:
// All values are compile-time constants and own no resources.
//
// Design notes:
// Runtime preferences belong to DockConfiguration and DockLayoutRequest.
//
// ------------------------------------------------------------

#pragma once

namespace DockConstants
{
    // Tooltip behaviour

    constexpr int TOOLTIP_SHOW_DELAY_MS = 200; // Delay before showing a tooltip
    constexpr int TOOLTIP_HIDE_DELAY_MS = 250; // Delay before hiding a tooltip

    // Leave one compositor frame between unmapping and mapping the tooltip so
    // its standard show animation is replayed for every dock item.
    constexpr int TOOLTIP_REMAP_DELAY_MS = 20; // Delay before remapping a tooltip
    constexpr int TOOLTIP_FADE_DURATION_MS = 180;
    constexpr int TOOLTIP_MOVE_DURATION_MS = 140;
    constexpr int PREVIEW_FADE_DURATION_MS = 220;
    constexpr int OVERLAY_ANIMATION_FRAME_MS = 16;

    // Autohide behaviour

    constexpr int AUTOHIDE_HIDE_DELAY_MS = 800;
    constexpr int AUTOHIDE_REVEAL_SIZE = 2;
    constexpr int AUTOHIDE_ANIMATION_DURATION_MS = 200;
    constexpr int AUTOHIDE_ANIMATION_FRAME_MS = 16;
    constexpr int GNOME_PLACEMENT_FALLBACK_DELAY_MS = 2000;

    // Internal drag-and-drop target shared by dock items and the dock surface.
    constexpr char DOCK_ITEM_DRAG_TARGET[] =
        "application/x-docklight-item";

    // Safety limits

    constexpr int MAX_DOCK_ITEMS = 50; // Maximum applications shown in the dock
    constexpr int TOOLTIP_MARGIN = 8;  // Minimum tooltip margin in pixels
}
