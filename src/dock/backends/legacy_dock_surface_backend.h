// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// legacy_dock_surface_backend.h
//
// Purpose:
// Owns ordinary GTK toplevel placement plus the existing X11 work-area and
// strut behavior behind the dock-surface backend interface.
//
// ------------------------------------------------------------

#pragma once

#include "dock_surface_backend.h"

#include <gio/gio.h>

#include <string>

class DockWindow;

class LegacyDockSurfaceBackend final
    : public IDockSurfaceBackend
{
public:
    LegacyDockSurfaceBackend(
        DockWindow &window,
        const Glib::RefPtr<Gdk::Monitor>
            &monitor);
    ~LegacyDockSurfaceBackend() override;

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
    bool initial_placement_pending() const override;
    void complete_initial_placement() override;

private:
    void capture_x11_base_workarea(
        const MonitorGeometry &output,
        const MonitorGeometry &fallback);
    void apply_x11_strut(
        const DockPlacement &placement,
        int x,
        int y,
        int width,
        int height);
    void apply_hyprland_reservation(
        const DockPlacement &placement,
        const MonitorGeometry &output,
        int width,
        int height);
    void clear_hyprland_reservation();

private:
    DockWindow &m_window;
    Glib::RefPtr<Gdk::Monitor> m_monitor;
    bool m_native_x11 = false;
    bool m_ordinary_wayland = false;
    bool m_initial_placement_pending = false;
    bool m_has_x11_base_workarea = false;
    MonitorGeometry m_x11_base_workarea;
    MonitorGeometry m_x11_base_output;
    GSubprocess *m_hyprland_reservation = nullptr;
    std::string m_hyprland_reservation_key;
};
