// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_layout_engine.h
//
// Purpose:
// Defines the layout engine responsible for calculating dock and
// tooltip placement from plain geometry values.
//
// Responsibilities:
// - Convert DockLayoutRequest into concrete screen placement.
// - Keep monitor-edge and orientation rules centralized.
// - Derive normal dock bounds without magnification capacity.
// - Clamp tooltip placement to the selected monitor.
// - Fit preview width to the actual space beside a vertical dock.
// - Avoid GTK and layer-shell side effects.
//
// Dependencies and ownership:
// Depends only on shared layout and window-geometry types. The engine
// owns no widgets, windows, or external resources.
//
// Design notes:
// DockWindowController applies calculated placement. Keeping the
// engine pure makes its rules reusable and independently testable.
//
// ------------------------------------------------------------

#pragma once

#include "dock_layout_types.h"
#include "dock_window_geometry.h"

class DockLayoutEngine
{
public:
    DockWindowGeometry normal_dock_geometry(
        DockLocation location,
        const DockWindowGeometry &surface,
        int normal_cross_axis_size,
        int main_axis_capacity) const;

    int preview_available_width(
        DockLocation location,
        const MonitorGeometry &monitor,
        const DockWindowGeometry &dock,
        int distance) const;

    DockPlacement
    calculate_dock_layout(
        const DockLayoutRequest &request,
        const MonitorGeometry &monitor,
        const DockWindowGeometry &dock) const;

    void apply_workarea_insets(
        DockPlacement &placement,
        const MonitorGeometry &output,
        const MonitorGeometry &workarea) const;

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
