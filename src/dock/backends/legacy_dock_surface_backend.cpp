// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// legacy_dock_surface_backend.cpp
//
// Implementation overview:
// Delegates placement and reservation clearing to DockWindow's unchanged
// implementation while exposing its current GDK monitor geometry.
//
// ------------------------------------------------------------

#include "legacy_dock_surface_backend.h"

#include "dock/dock_window.h"
#include "layout/dock_layout_geometry.h"

LegacyDockSurfaceBackend::
    LegacyDockSurfaceBackend(
        DockWindow &window,
        const Glib::RefPtr<Gdk::Monitor>
            &monitor)
    : m_window(window),
      m_monitor(monitor)
{
}

void LegacyDockSurfaceBackend::set_monitor(
    const Glib::RefPtr<Gdk::Monitor>
        &monitor)
{
    m_monitor = monitor;
}

MonitorGeometry
LegacyDockSurfaceBackend::output_geometry() const
{
    DockLayoutGeometry geometry;
    return geometry.output_geometry(m_monitor);
}

MonitorGeometry
LegacyDockSurfaceBackend::work_area() const
{
    DockLayoutGeometry geometry;
    return geometry.monitor_geometry(m_monitor);
}

void LegacyDockSurfaceBackend::
    apply_dock_placement(
        const DockPlacement &placement,
        const MonitorGeometry &output,
        const MonitorGeometry &work_area)
{
    m_window.apply_legacy_dock_layout(
        placement,
        output,
        work_area);
}

void LegacyDockSurfaceBackend::reserve_space(
    const DockPlacement &)
{
    // The legacy placement method still applies layer-shell exclusive zones
    // and X11 struts atomically with placement. This becomes a real boundary
    // when those implementations are extracted in later migration steps.
}

void LegacyDockSurfaceBackend::clear_reserved_space()
{
    m_window.clear_legacy_reserved_space();
}
