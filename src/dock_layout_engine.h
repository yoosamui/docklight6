#pragma once

#include "dock_layout_types.h"
#include "dock_window_geometry.h"

//
// Calculates the runtime position of dock-related windows.
//
// This class performs no rendering.
// It only computes screen positions.
//
// Responsibility:
//
// Calculates screen positions for dock windows.
//
class DockLayoutEngine
{
public:


    DockPlacement
    calculate_dock_layout(
        const DockLayoutRequest &request,
        const MonitorGeometry &monitor,
        const DockWindowGeometry &dock) const;

    ScreenPosition
    calculate_tooltip_position(
        const DockLayoutRequest &request,
        const MonitorGeometry &monitor,
        const DockWindowGeometry &dock,
        const ItemGeometry &item,
        int tooltip_width,
        int tooltip_height,
        int tooltip_distance) const;
};
