#pragma once

namespace DockConstants
{
    // Tooltip behaviour

    constexpr int TOOLTIP_SHOW_DELAY_MS = 200;
    constexpr int TOOLTIP_HIDE_DELAY_MS = 250;

    // Leave one compositor frame between unmapping and mapping the tooltip so
    // its standard show animation is replayed for every dock item.
    constexpr int TOOLTIP_REMAP_DELAY_MS = 20;

    // Safety limits

    constexpr int MAX_DOCK_ITEMS = 20;
    constexpr int TOOLTIP_MARGIN = 8;
}
