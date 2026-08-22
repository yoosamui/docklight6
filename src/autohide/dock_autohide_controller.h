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
// - Publish reveal completion for position-dependent overlays.
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
#include <sigc++/signal.h>

class DockWindow;

class DockAutohideController
{
public:
    explicit DockAutohideController(
        DockWindow &window,
        int hide_delay_ms);
    ~DockAutohideController();

    void initialize();
    void begin_initial_x11_startup();
    void complete_initial_x11_startup();
    void set_mode(DockAutohide mode);
    void set_effect(DockAutohideEffect effect);
    void set_hide_delay(int delay_ms);
    void set_monitor(
        const Glib::RefPtr<Gdk::Monitor> &monitor);
    void set_placement(
        const DockPlacement &placement,
        const ScreenPosition &shown_position);
    void set_intellihide_overlap(bool overlap);

    void inhibit();
    void uninhibit(bool pointer_inside);
    void finish_drag(bool pointer_inside);
    void refresh_mapped_surface();
    void request_reveal();
    void set_backend_pointer_inside(bool inside);
    void finish_shell_animation(bool hidden);

    bool is_fully_revealed() const;
    sigc::signal<void> &signal_fully_revealed();

private:
    void pointer_entered();
    void pointer_left();
    void schedule_hide(bool refresh_pointer = true);
    void cancel_hide();
    void cancel_animation();
    void reset_x11_visual_transform();
    void apply_hidden_x11_placement(
        const ScreenPosition &shown_position);
    void request_shell_visibility(bool hidden);
    void set_surface_input_passthrough(bool passthrough);
    void animate_effect(bool hiding);
    void animate_fade(bool hiding);
    bool advance_fade_animation();
    bool can_animate_x11() const;
    bool should_collapse_x11_horizontally() const;
    void animate_x11(
        bool hiding,
        bool start_at_hidden_edge = false);
    bool advance_x11_animation();
    void hide_immediately_for_x11_startup();
    void reveal_immediately();
    void hide_now(bool refresh_pointer = true);
    void reveal();
    bool can_hide() const;
    bool pointer_inside() const;
    bool has_shell_reveal_trigger() const;
    bool uses_shell_reveal_trigger() const;
    bool uses_backend_pointer_tracking() const;

private:
    DockWindow &m_window;
    DockRevealWindow m_reveal_window;

    sigc::connection m_pointer_enter;
    sigc::connection m_pointer_leave;
    sigc::connection m_window_map;
    sigc::connection m_reveal_requested;
    sigc::connection m_hide_timer;
    sigc::connection m_animation_timer;
    sigc::connection m_x11_reveal_start_timer;

    enum class ShellDockState
    {
        visible,
        revealing,
        hiding,
        hidden
    };

    DockAutohide m_mode = DockAutohide::none;
    DockAutohideEffect m_effect =
        DockAutohideEffect::slide;
    int m_hide_delay_ms = 0;
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
    double m_animation_start_scale = 1.0;
    double m_animation_target_scale = 1.0;
    double m_animation_start_vertical_offset = 0.0;
    double m_animation_target_vertical_offset = 0.0;
    double m_animation_start_opacity = 1.0;
    double m_animation_target_opacity = 1.0;
    gint64 m_animation_start_time_us = 0;
    int m_animation_duration_ms = 0;
    bool m_initialized = false;
    bool m_hidden = false;
    bool m_has_shown_position = false;
    bool m_animating_to_hidden = false;
    bool m_animation_collapses_horizontally = false;
    bool m_animation_clips_top = false;
    bool m_animation_scale_anchor_right = true;
    bool m_pointer_inside = false;
    bool m_shell_pointer_inside = false;
    bool m_backend_pointer_inside = false;
    bool m_suppress_next_map_hide = false;
    bool m_pending_x11_reveal_animation = false;
    bool m_initial_x11_startup_pending = false;
    ShellDockState m_shell_state = ShellDockState::visible;

    sigc::signal<void> m_signal_fully_revealed;
};
