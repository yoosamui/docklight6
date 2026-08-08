// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
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

    // Tooltip

    // All tooltip values use the same 48px baseline as the original dock.
    // Scaling from the icon size keeps the tooltip proportionate to its dock.
    static constexpr int BASE_ICON_SIZE = 48; // Baseline used to scale visual metrics
    static constexpr int TOOLTIP_MIN_WIDTH = 80; // Baseline tooltip minimum width
    static constexpr int TOOLTIP_HEIGHT = 38; // Baseline tooltip height
    static constexpr int TOOLTIP_DISTANCE = 12; // Baseline gap from the dock
    // Minimum gap between a tooltip and either end of the monitor axis.
    static constexpr int TOOLTIP_EDGE_MARGIN = 8; // Minimum gap from monitor edges
    static constexpr int TOOLTIP_LABEL_PADDING = 12; // Baseline horizontal label padding
    static constexpr int TOOLTIP_FONT_SIZE = 12; // Baseline tooltip font size

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

    static constexpr int HOVER_ZOOM_PERCENT = 120; // Icon scale at maximum zoom
};
