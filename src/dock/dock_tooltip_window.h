// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_tooltip_window.h
//
// Purpose:
// Declares the non-interactive layer-shell window used for dock labels.
//
// Responsibilities:
// - Measure text before layout placement is calculated.
// - Apply a calculated position without deriving screen geometry.
// - Manage tooltip styling, remapping, and input transparency.
//
// Dependencies and ownership:
// The GTK window owns its child widgets, CSS provider reference, and
// reveal timer. Monitor references use shared Glib ownership.
//
// Design notes:
// Placement remains in DockLayoutEngine; this class performs only the
// GTK and layer-shell side effects needed to display the result.
//
// ------------------------------------------------------------

#pragma once

#include "layout/dock_layout_metrics.h"
#include "layout/dock_layout_types.h"

#include <gdkmm/monitor.h>
#include <glibmm/ustring.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/eventbox.h>
#include <gtkmm/label.h>
#include <gtkmm/window.h>
#include <sigc++/sigc++.h>

class DockTooltipWindow : public Gtk::Window
{
public:
    DockTooltipWindow();

    void set_monitor(
        const Glib::RefPtr<Gdk::Monitor>
            &monitor);

    void set_rounded_corners(
        bool enabled,
        int radius,
        int icon_size);
    void show_tooltip(
        const Glib::ustring &text,
        DockLocation location,
        int tooltip_width,
        const ScreenPosition &position);
    void hide_tooltip();
    void hide_tooltip_immediately();

    // The layout engine must receive this width before calculating the
    // position, otherwise a variable-width tooltip would not stay centered.
    int preferred_width_for(const Glib::ustring &text);
    int tooltip_height() const;
    int tooltip_distance() const;

private:
    void cancel_reveal();
    void cancel_opacity_animation();
    void cancel_motion_animation();
    void start_opacity_animation(bool hiding);
    bool advance_opacity_animation();
    void start_motion_animation(
        const ScreenPosition &position,
        int width);
    bool advance_motion_animation();
    void make_input_transparent();
    void apply_position(
        DockLocation location,
        const ScreenPosition &position,
        int width,
        int height);

    bool is_current_request(
        const Glib::ustring &text,
        DockLocation location,
        int tooltip_width,
        const ScreenPosition &position) const;

private:
    Glib::RefPtr<Gtk::CssProvider> m_visual_css;
    Glib::ustring m_request_text;

    Gtk::Label m_label;
    Gtk::EventBox m_event_box;

    sigc::connection m_reveal_timer;
    sigc::connection m_opacity_timer;
    sigc::connection m_motion_timer;

    DockLocation m_request_location =
        DockLocation::bottom;

    ScreenPosition m_request_position;
    ScreenPosition m_displayed_position;
    ScreenPosition m_motion_start_position;
    ScreenPosition m_motion_target_position;
    MonitorGeometry m_monitor_geometry;

    int m_tooltip_height = DockLayoutMetrics::TOOLTIP_HEIGHT;
    int m_tooltip_distance = DockLayoutMetrics::TOOLTIP_DISTANCE;
    int m_icon_size = DockLayoutMetrics::BASE_ICON_SIZE;
    int m_request_width = 0;
    int m_displayed_width = 0;
    int m_motion_start_width = 0;
    int m_motion_target_width = 0;
    gint64 m_opacity_animation_start_us = 0;
    gint64 m_motion_animation_start_us = 0;
    double m_opacity_animation_start = 1.0;
    double m_opacity_animation_target = 1.0;
    int m_animation_start_x = 0;
    int m_animation_start_y = 0;
    int m_animation_target_x = 0;
    int m_animation_target_y = 0;

    bool m_has_request = false;
    bool m_animation_moves_window = false;
    bool m_opacity_animation_hiding = false;
    bool m_uses_layer_shell = false;
};
