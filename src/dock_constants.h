// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// Defines shared interaction limits, timing defaults, and protocol
// identifiers used by dock items, tooltips, and the dock surface.
//
// These compile-time values own no resources. Runtime layout choices
// belong to DockConfiguration and DockLayoutRequest.
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

    // Autohide behaviour

    constexpr int AUTOHIDE_HIDE_DELAY_MS = 800;
    constexpr int AUTOHIDE_REVEAL_SIZE = 2;

    // Internal drag-and-drop target shared by dock items and the dock surface.
    constexpr char DOCK_ITEM_DRAG_TARGET[] =
        "application/x-docklight-item";

    // Safety limits

    constexpr int MAX_DOCK_ITEMS = 50; // Maximum applications shown in the dock
    constexpr int TOOLTIP_MARGIN = 8;  // Minimum tooltip margin in pixels
}
