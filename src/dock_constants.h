#pragma once

namespace DockConstants
{
    // Tooltip behaviour

    constexpr int TOOLTIP_SHOW_DELAY_MS = 200; // Delay before showing a tooltip
    constexpr int TOOLTIP_HIDE_DELAY_MS = 250; // Delay before hiding a tooltip

    // Leave one compositor frame between unmapping and mapping the tooltip so
    // its standard show animation is replayed for every dock item.
    constexpr int TOOLTIP_REMAP_DELAY_MS = 20; // Delay before remapping a tooltip

    // Internal drag-and-drop target shared by dock items and the dock surface.
    constexpr char DOCK_ITEM_DRAG_TARGET[] =
        "application/x-docklight-item";

    // Safety limits

    constexpr int MAX_DOCK_ITEMS = 20; // Maximum applications shown in the dock
    constexpr int TOOLTIP_MARGIN = 8; // Minimum tooltip margin in pixels
}
