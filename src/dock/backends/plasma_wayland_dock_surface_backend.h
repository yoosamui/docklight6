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

    MonitorGeometry
    effective_work_area(
        const MonitorGeometry &output,
        const MonitorGeometry &work_area) override;

    void apply_dock_placement(
        const DockPlacement &placement,
        const MonitorGeometry &output,
        const MonitorGeometry &work_area) override;

    void reserve_space(
        const DockPlacement &placement) override;

    void clear_reserved_space() override;

    bool uses_native_placement() const override;
    bool is_native_x11() const override;
    bool is_ordinary_wayland() const override;
    DockAutohideEffect
    default_autohide_effect() const override;
    std::vector<DockAutohideEffect>
    configurable_autohide_effects() const override;
    bool delegates_autohide_effect(
        DockAutohideEffect effect) const override;
    double autohide_fade_opacity() const override;
    void set_autohide_fade_opacity(
        double opacity) override;
    void finish_autohide_fade(
        bool hidden) override;
    bool supports_autohide_slide() const override;
    double autohide_slide_progress() const override;
    void set_autohide_slide_progress(
        const DockPlacement &placement,
        double progress) override;
    void finish_autohide_slide(
        bool hidden) override;
    bool initial_placement_pending() const override;
    void complete_initial_placement() override;

private:
    DockWindow &m_window;
    Glib::RefPtr<Gdk::Monitor> m_monitor;
    double m_autohide_slide_progress = 0.0;
};
