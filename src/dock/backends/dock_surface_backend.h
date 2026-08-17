// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_surface_backend.h
//
// Purpose:
// Declares the backend boundary used to place and reserve space for the
// main dock surface.
//
// Responsibilities:
// - Expose selected-monitor geometry to dock layout orchestration.
// - Apply a calculated DockPlacement without exposing backend APIs.
// - Own backend-specific screen reservation side effects.
//
// Dependencies and ownership:
// Implementations borrow the dock surface and share the selected monitor.
// The interface owns no GTK or native display resources.
//
// Design notes:
// This interface is intentionally distinct from WindowBackend, which
// observes and controls application windows.
//
// ------------------------------------------------------------

#pragma once

#include "layout/dock_layout_types.h"

#include <gdkmm/monitor.h>

#include <memory>

class DockWindow;

class IDockSurfaceBackend
{
public:
    virtual ~IDockSurfaceBackend() = default;

    virtual void set_monitor(
        const Glib::RefPtr<Gdk::Monitor>
            &monitor) = 0;

    virtual MonitorGeometry
    output_geometry() const = 0;

    virtual MonitorGeometry
    work_area() const = 0;

    virtual MonitorGeometry
    effective_work_area(
        const MonitorGeometry &output,
        const MonitorGeometry &work_area) = 0;

    // Placement is fully resolved by DockLayoutEngine. Backends consume the
    // logical result and translate it to their native surface mechanism.
    virtual void apply_dock_placement(
        const DockPlacement &placement,
        const MonitorGeometry &output,
        const MonitorGeometry &work_area) = 0;

    virtual void reserve_space(
        const DockPlacement &placement) = 0;

    virtual void clear_reserved_space() = 0;

    virtual bool uses_native_placement() const = 0;
    virtual bool is_native_x11() const = 0;
    virtual bool is_ordinary_wayland() const = 0;

    virtual bool initial_placement_pending() const = 0;
    virtual void complete_initial_placement() = 0;
};

std::unique_ptr<IDockSurfaceBackend>
create_dock_surface_backend(
    DockWindow &window,
    const Glib::RefPtr<Gdk::Monitor> &monitor);
