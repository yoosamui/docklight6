// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_reveal_window.h
//
// Purpose:
// Declares the transparent edge surface used to reveal an autohidden dock.
//
// Responsibilities:
// - Track the selected monitor and dock placement.
// - Apply layer-shell geometry for the active screen edge.
// - Emit a reveal request when the pointer enters.
//
// Dependencies and ownership:
// The object owns its GTK surface and signal; monitor references use GLib
// ownership.
//
// Design notes:
// The reveal surface remains separate from the visible dock window.
//
// ------------------------------------------------------------

#pragma once

#include "layout/dock_layout_types.h"

#include <gdkmm/monitor.h>
#include <gtkmm/window.h>
#include <sigc++/signal.h>

class DockRevealWindow : public Gtk::Window
{
public:
    DockRevealWindow();
    ~DockRevealWindow() override;

    void set_monitor(
        const Glib::RefPtr<Gdk::Monitor> &monitor);
    void apply_placement(
        const DockPlacement &placement);

    sigc::signal<void> &signal_reveal_requested();

private:
    enum class SurfaceBackend
    {
        layer_shell,
        wayland_toplevel,
        x11
    };

    void prepare_reconfiguration();
    void apply_wayland_toplevel_placement();
    void apply_x11_placement();
    void start_x11_edge_poll();
    void stop_x11_edge_poll();
    bool poll_x11_physical_edge();
    bool has_x11_panel_inset() const;
    bool on_enter_notify_event(
        GdkEventCrossing *event) override;

private:
    sigc::signal<void> m_signal_reveal_requested;
    Glib::RefPtr<Gdk::Monitor> m_monitor;
    MonitorGeometry m_monitor_geometry;
    DockPlacement m_placement;
    sigc::connection m_x11_edge_poll_timer;
    bool m_has_placement = false;
    bool m_pointer_was_on_physical_edge = false;
    SurfaceBackend m_backend = SurfaceBackend::x11;
};
