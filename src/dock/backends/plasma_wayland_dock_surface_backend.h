// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// plasma_wayland_dock_surface_backend.h
//
// Purpose:
// Applies native Plasma Wayland placement and reservation policy to the
// main DockWindow layer surface.
//
// ------------------------------------------------------------

#pragma once

#include "dock_surface_backend.h"

class DockWindow;

class PlasmaWaylandDockSurfaceBackend final
    : public IDockSurfaceBackend
{
public:
    PlasmaWaylandDockSurfaceBackend(
        DockWindow &window,
        const Glib::RefPtr<Gdk::Monitor>
            &monitor);

    void set_monitor(
        const Glib::RefPtr<Gdk::Monitor>
            &monitor) override;

    MonitorGeometry
    output_geometry() const override;

    MonitorGeometry
    work_area() const override;

    void apply_dock_placement(
        const DockPlacement &placement,
        const MonitorGeometry &output,
        const MonitorGeometry &work_area) override;

    void reserve_space(
        const DockPlacement &placement) override;

    void clear_reserved_space() override;

private:
    DockWindow &m_window;
    Glib::RefPtr<Gdk::Monitor> m_monitor;
};
