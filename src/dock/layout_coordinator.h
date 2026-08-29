// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// layout_coordinator.h
//
// Purpose:
// Declares monitor and work-area normalization for dock overlay layout.
//
// Responsibilities:
// - Resolve output, native work-area, and sizing geometry.
// - Produce monitor-local usable geometry for tooltip and preview layout.
// - Intersect compositor-reported work areas with the selected output.
//
// Dependencies and ownership:
// LayoutCoordinator borrows DockWindow and returns plain geometry values; it
// owns no GTK surfaces or compositor resources.
//
// Design notes:
// This module calculates coordinate spaces but does not apply placement side
// effects to dock surfaces.
//
// ------------------------------------------------------------

#pragma once

#include "config/dock_configuration.h"
#include "layout/dock_layout_types.h"
#include "windowing/window_icon_geometry.h"

class DockWindow;

struct DockMonitorLayout
{
    MonitorGeometry output;
    MonitorGeometry native_workarea;
    MonitorGeometry sizing_workarea;
    MonitorGeometry usable_monitor;

    bool valid() const
    {
        return output.width > 0 && output.height > 0;
    }
};

class LayoutCoordinator
{
public:
    explicit LayoutCoordinator(DockWindow &window);

    DockMonitorLayout resolve_monitor_layout(
        const DockSettings &settings,
        const DockLayoutRequest &request) const;

private:
    static bool intersect_workarea_with_output(
        const WindowIconGeometry &reported,
        const MonitorGeometry &output,
        MonitorGeometry &workarea);

private:
    DockWindow &m_window;
};
