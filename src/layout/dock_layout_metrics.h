// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_layout_metrics.h
//
// Purpose:
// Declares visual baselines and pure scaling helpers for dock, tooltip,
// preview, and hover dimensions.
//
// Responsibilities:
// - Centralize size relationships used across widgets.
// - Scale baseline values from the configured icon size.
// - Keep visual measurements consistent across layout consumers.
//
// Dependencies and ownership:
// The class owns no state or resources; all helpers operate on supplied
// values.
//
// Design notes:
// Runtime preferences remain inputs rather than global mutable settings.
//
// ------------------------------------------------------------

#pragma once

#include <algorithm>
#include <cmath>

class DockLayoutMetrics
{
public:
    // Dock

    static constexpr int DOCK_HEIGHT = 48; // Baseline dock height in pixels
    // Empty space inside the dock before the first and after the last item.
    static constexpr int DOCK_MARGIN = 8; // Horizontal dock content margin

    // Icon size is the primary user setting. Item size is derived from it so
    // icon, padding, indicators, and later hover animation share one scale.
    static constexpr int DOCK_ITEM_PADDING = 8; // Space around each application icon

    static int item_size_for(int icon_size)
    {
        return icon_size + 2 * DOCK_ITEM_PADDING;
    }

    static int corner_radius_for(int icon_size)
    {
        return item_size_for(icon_size) / 2;
    }

    static int magnified_main_axis_extra_for(
        int icon_size)
    {
        icon_size = std::max(1, icon_size);
        const int maximum_icon_extent =
            static_cast<int>(std::lround(
                icon_size * 2.25)) +
            2 * DOCK_ITEM_PADDING;
        const int overflow = std::max(
            0,
            maximum_icon_extent -
                item_size_for(icon_size));
        // The visible group is centered in this capacity. Keep it even so
        // both ends can own an exact integer half without a one-pixel resize
        // at maximum scale.
        return overflow + overflow % 2;
    }

    static int fitted_icon_size_for(
        int requested_icon_size,
        int item_count,
        int monitor_length,
        bool magnified)
    {
        int icon_size =
            std::max(1, requested_icon_size);
        if (item_count <= 0 || monitor_length <= 0)
            return icon_size;

        const long long available_length =
            std::max(
                1,
                monitor_length - 2 * DOCK_MARGIN);

        const auto required_length =
            [item_count, magnified](int candidate)
            {
                return static_cast<long long>(item_count) *
                           item_size_for(candidate) +
                       (magnified
                            ? magnified_main_axis_extra_for(
                                  candidate)
                            : 0);
            };

        while (icon_size > 1 &&
               required_length(icon_size) >
                   available_length)
        {
            --icon_size;
        }

        return icon_size;
    }

    // Tooltip

    // All tooltip values use the same 48px baseline as the original dock.
    // Scaling from the icon size keeps the tooltip proportionate to its dock.
    static constexpr int BASE_ICON_SIZE = 48; // Baseline used to scale visual metrics
    static constexpr int TOOLTIP_MIN_WIDTH = 80; // Baseline tooltip minimum width
    static constexpr int TOOLTIP_HEIGHT = 38; // Baseline tooltip height
    static constexpr int TOOLTIP_DISTANCE = 12; // Normal gap from the dock
    // Magnified icons grow into the transparent overflow reserved around the
    // dock. Place tooltips and previews flush with that surface edge.
    static constexpr int MAGNIFIED_TOOLTIP_DISTANCE = 0;
    // Minimum gap between a tooltip and either end of the monitor axis.
    static constexpr int TOOLTIP_EDGE_MARGIN = 8; // Minimum gap from monitor edges
    static constexpr int TOOLTIP_LABEL_PADDING = 12; // Baseline horizontal label padding
    static constexpr int TOOLTIP_FONT_SIZE = 12; // Baseline tooltip font size

    static int scale_from_icon_size(
        int value,
        int icon_size)
    {
        // Zero and negative distances are meaningful for overlay placement;
        // preserve their sign instead of clamping every metric to one pixel.
        if (value == 0)
            return 0;

        const int magnitude = std::max(
            1,
            (std::abs(value) * icon_size + BASE_ICON_SIZE / 2) /
                BASE_ICON_SIZE);
        return value < 0
                   ? -magnitude
                   : magnitude;
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

    static int magnified_tooltip_distance_for(
        int icon_size)
    {
        return scale_from_icon_size(
            MAGNIFIED_TOOLTIP_DISTANCE,
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

    static constexpr int HOVER_ZOOM_PERCENT = 120; // Icon scale at maximum zoom
};
