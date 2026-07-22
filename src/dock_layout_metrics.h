#pragma once

#include <algorithm>

//
// Stores compile-time visual metrics used by the dock.
//
// Runtime choices belong in DockLayoutSettings and placement decisions belong
// in DockLayoutEngine.
//
// Responsibility:
//
// Stores the layout configuration of the dock.
//
class DockLayoutMetrics
{

public:
    // Dock

    static constexpr int DOCK_HEIGHT = 48;
    static constexpr int DOCK_MARGIN = 8;

    // Icon size is the primary user setting. Item size is derived from it so
    // icon, padding, indicators, and later hover animation share one scale.
    static constexpr int DOCK_ITEM_PADDING = 8;

    static int item_size_for(int icon_size)
    {
        return icon_size + 2 * DOCK_ITEM_PADDING;
    }

    static int corner_radius_for(int icon_size)
    {
        return item_size_for(icon_size) / 2;
    }

    // Tooltip

    // All tooltip values use the same 48px baseline as the original dock.
    // Scaling from the icon size keeps the tooltip proportionate to its dock.
    static constexpr int BASE_ICON_SIZE = 48;
    static constexpr int TOOLTIP_MIN_WIDTH = 80;
    static constexpr int TOOLTIP_HEIGHT = 38;
    static constexpr int TOOLTIP_DISTANCE = 12;
    static constexpr int TOOLTIP_LABEL_PADDING = 12;
    static constexpr int TOOLTIP_FONT_SIZE = 12;

    static int scale_from_icon_size(
        int value,
        int icon_size)
    {
        // A zero distance is a meaningful user setting: it requests direct
        // contact between tooltip and dock, not a minimum one-pixel gap.
        if (value == 0)
            return 0;

        return std::max(
            1,
            (value * icon_size + BASE_ICON_SIZE / 2) /
                BASE_ICON_SIZE);
    }

    static int tooltip_min_width_for(int icon_size)
    {
        return scale_from_icon_size(
            TOOLTIP_MIN_WIDTH,
            icon_size);
    }

    static int tooltip_height_for(int icon_size)
    {
        return scale_from_icon_size(
            TOOLTIP_HEIGHT,
            icon_size);
    }

    static int tooltip_distance_for(int icon_size)
    {
        return scale_from_icon_size(
            TOOLTIP_DISTANCE,
            icon_size);
    }

    static int tooltip_label_padding_for(int icon_size)
    {
        return scale_from_icon_size(
            TOOLTIP_LABEL_PADDING,
            icon_size);
    }

    static int tooltip_font_size_for(int icon_size)
    {
        return scale_from_icon_size(
            TOOLTIP_FONT_SIZE,
            icon_size);
    }

    // Animation

    static constexpr int HOVER_SCALE = 2;
};
