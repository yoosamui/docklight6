// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// layout_coordinator.cpp
//
// Implementation overview:
// Normalizes GTK, surface-backend, and window-integration geometry into the
// coordinate spaces consumed by dock, tooltip, and preview layout.
//
// Important implementation decisions:
// - EWMH remains authoritative for native X11 and XWayland surfaces.
// - Native Wayland may use compositor-reported monitor work areas.
// - Configured bottom inset requirements affect sizing without changing the
//   recorded native work area.
// - Reported work areas are clipped to the selected output.
//
// ------------------------------------------------------------

#include "layout_coordinator.h"

#include "dock_window.h"
#include "windowing/window_icon_geometry.h"
#include "windowing/window_registry.h"

#include <algorithm>

LayoutCoordinator::LayoutCoordinator(DockWindow &window)
    : m_window(window)
{
}

DockMonitorLayout LayoutCoordinator::resolve_monitor_layout(
    const DockSettings &settings,
    const DockLayoutRequest &request) const
{
    DockMonitorLayout result;
    result.output = m_window.surface_output_geometry();
    if (!result.valid())
        return result;

    auto workarea = m_window.surface_work_area();
    if (workarea.width <= 0 || workarea.height <= 0)
        workarea = result.output;

    const bool x11_dock = m_window.surface_is_native_x11();
    workarea = m_window.surface_effective_work_area(
        result.output,
        workarea);

    // GTK 3 cannot report Plasma's monitor-scoped Wayland work area. Prefer
    // the KWin integration report for native Wayland surfaces, while keeping
    // EWMH authoritative for an X11 or XWayland dock.
    if (m_window.m_window_registry && !x11_dock)
    {
        const auto reported =
            m_window.m_window_registry->dock_workarea_geometry();
        MonitorGeometry kwin_workarea;
        if (reported && intersect_workarea_with_output(
                            *reported,
                            result.output,
                            kwin_workarea))
        {
            workarea = kwin_workarea;
        }
    }

    result.native_workarea = workarea;

    const int reported_bottom_inset = std::max(
        0,
        result.output.y + result.output.height -
            workarea.y - workarea.height);
    const int required_bottom_inset = std::max(
        reported_bottom_inset,
        settings.minimum_bottom_workarea_inset());
    const int missing_bottom_inset =
        request.location == DockLocation::bottom
            ? std::max(
                  0,
                  required_bottom_inset - reported_bottom_inset)
            : 0;

    workarea.height = std::max(1, workarea.height - missing_bottom_inset);
    result.sizing_workarea = workarea;
    result.usable_monitor = {
        workarea.x - result.output.x,
        workarea.y - result.output.y,
        workarea.width,
        workarea.height};
    return result;
}

bool LayoutCoordinator::intersect_workarea_with_output(
    const WindowIconGeometry &reported,
    const MonitorGeometry &output,
    MonitorGeometry &workarea)
{
    if (reported.width <= 0 || reported.height <= 0)
        return false;

    const int left = std::max(output.x, reported.x);
    const int top = std::max(output.y, reported.y);
    const int right = std::min(
        output.x + output.width,
        reported.x + reported.width);
    const int bottom = std::min(
        output.y + output.height,
        reported.y + reported.height);
    if (right <= left || bottom <= top)
        return false;

    workarea = {left, top, right - left, bottom - top};
    return true;
}
