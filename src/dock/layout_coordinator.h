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

// Resolves compositor-specific output/work-area reports into the three
// coordinate spaces consumed by dock, tooltip, and preview layout.
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
