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
#include <vector>

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

    // The shared controller owns the selected effect. Each surface backend
    // supplies the default which preserves its current visual behavior.
    virtual DockAutohideEffect
    default_autohide_effect() const = 0;

    // Settings only exposes effects which the active surface backend can
    // implement without changing presentation mode. Plasma Wayland keeps
    // its compositor map/unmap effect and adds its client-rendered
    // slide/fade; GNOME Wayland delegates its choices to Shell; native X11
    // retains its existing choices.
    virtual std::vector<DockAutohideEffect>
    configurable_autohide_effects() const = 0;

    // Physical effect ownership stays at the surface boundary. A delegated
    // effect is animated by the compositor integration; otherwise fade uses
    // the backend's native surface opacity and hidden-state operation.
    virtual bool delegates_autohide_effect(
        DockAutohideEffect effect) const = 0;
    virtual double autohide_fade_opacity() const = 0;
    virtual void set_autohide_fade_opacity(
        double opacity) = 0;
    virtual void finish_autohide_fade(
        bool hidden) = 0;

    // Native Plasma Wayland owns its physical slide/fade transform. The
    // shared controller supplies normalized progress (0 shown, 1 hidden) so
    // policy and timing remain independent of layer-surface drawing details.
    // GNOME Wayland instead delegates this effect through the compositor
    // integration and therefore does not use these local-surface hooks.
    virtual bool supports_autohide_slide_fade() const
    {
        return false;
    }
    virtual double autohide_slide_fade_progress() const
    {
        return 0.0;
    }
    virtual void set_autohide_slide_fade_progress(
        const DockPlacement &,
        double)
    {
    }
    virtual void finish_autohide_slide_fade(
        bool)
    {
    }

    virtual bool initial_placement_pending() const = 0;
    virtual void complete_initial_placement() = 0;
};

std::unique_ptr<IDockSurfaceBackend>
create_dock_surface_backend(
    DockWindow &window,
    const Glib::RefPtr<Gdk::Monitor> &monitor);
