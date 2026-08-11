// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_autohide_controller.h
//
// Purpose:
// Declares the controller for dock autohide lifecycle, timing, and
// edge-trigger coordination.
//
// Responsibilities:
// - Apply configured autohide modes and monitor placement.
// - Coordinate dock visibility with pointer and overlap state.
// - Manage temporary visibility inhibition.
//
// Dependencies and ownership:
// The controller borrows DockWindow and owns its reveal window, timers, and
// signal connections.
//
// Design notes:
// Visibility policy is centralized outside the dock widget.
//
// ------------------------------------------------------------

#pragma once

#include "layout/dock_layout_types.h"
#include "dock_reveal_window.h"

#include <gdkmm/monitor.h>
#include <sigc++/connection.h>

class DockWindow;

class DockAutohideController
{
public:
    explicit DockAutohideController(
        DockWindow &window);
    ~DockAutohideController();

    void initialize();
    void set_mode(DockAutohide mode);
    void set_monitor(
        const Glib::RefPtr<Gdk::Monitor> &monitor);
    void set_placement(
        const DockPlacement &placement);
    void set_intellihide_overlap(bool overlap);

    void inhibit();
    void uninhibit(bool pointer_inside);
    void finish_drag(bool pointer_inside);
    void refresh_mapped_surface();
    void request_reveal();

private:
    void pointer_entered();
    void pointer_left();
    void schedule_hide(bool refresh_pointer = true);
    void cancel_hide();
    void cancel_animation();
    void set_shell_dock_hidden(bool hidden);
    void animate_shell_opacity(bool hiding);
    bool can_animate_x11() const;
    ScreenPosition hidden_x11_position() const;
    void animate_x11(
        bool hiding,
        bool start_at_hidden_edge = false);
    bool advance_x11_animation();
    void reveal_immediately();
    void hide_now(bool refresh_pointer = true);
    void reveal();
    bool can_hide() const;
    bool uses_shell_reveal_trigger() const;

private:
    DockWindow &m_window;
    DockRevealWindow m_reveal_window;

    sigc::connection m_pointer_enter;
    sigc::connection m_pointer_leave;
    sigc::connection m_pointer_motion;
    sigc::connection m_window_map;
    sigc::connection m_reveal_requested;
    sigc::connection m_hide_timer;
    sigc::connection m_animation_timer;
    sigc::connection m_x11_reveal_start_timer;
    sigc::connection m_shell_opacity_animation_timer;

    DockAutohide m_mode = DockAutohide::none;
    int m_inhibit_count = 0;
    bool m_intellihide_overlap = false;
    bool m_has_placement = false;
    DockPlacement m_placement;
    int m_shown_x = 0;
    int m_shown_y = 0;
    int m_animation_start_x = 0;
    int m_animation_start_y = 0;
    int m_animation_target_x = 0;
    int m_animation_target_y = 0;
    gint64 m_animation_start_time_us = 0;
    int m_animation_duration_ms = 0;
    bool m_initialized = false;
    bool m_hidden = false;
    bool m_has_shown_position = false;
    bool m_animating_to_hidden = false;
    bool m_pointer_inside = false;
    bool m_suppress_next_map_hide = false;
    bool m_pending_x11_reveal_animation = false;
    bool m_shell_edge_reveal = false;
    double m_shell_opacity_start = 1.0;
    double m_shell_opacity_target = 1.0;
    gint64 m_shell_opacity_start_time_us = 0;
};
