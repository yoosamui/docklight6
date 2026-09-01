// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window_controller.h
//
// Purpose:
// Declares the façade coordinating dock layout, autohide, tooltip, preview,
// and icon-geometry services.
//
// Responsibilities:
// - Recalculate layout when configuration or monitor state changes.
// - Clamp effective icon size to the selected output.
// - Forward tooltip and preview requests to focused managers.
// - Schedule icon refresh and geometry updates.
// - Keep DockWindow side effects ordered after GTK realization.
//
// Dependencies and ownership:
// The controller borrows its DockWindow, shares monitor and icon-theme
// references, and owns calculation objects and signal connections.
//
// Design notes:
// The controller retains only cross-cutting policy and final dock layout;
// surface-specific state and timers live in the owned coordinators.
//
// ------------------------------------------------------------

#pragma once

#include "config/dock_configuration.h"
#include "layout/dock_layout_engine.h"
#include "layout/dock_layout_geometry.h"
#include "windowing/managed_window.h"

#include <gdkmm/monitor.h>
#include <glibmm/ustring.h>
#include <gtkmm/icontheme.h>
#include <sigc++/connection.h>

#include <memory>
#include <string>

class DockWindow;
class DockAutohideController;
class DockItem;
class TooltipManager;
class PreviewManager;
class LayoutCoordinator;

namespace Gtk
{
class Widget;
}

class DockWindowController
{
public:
    DockWindowController(
        DockWindow &window,
        const DockConfiguration &configuration,
        const Glib::RefPtr<Gdk::Monitor>
            &monitor);
    ~DockWindowController();

    void initialize();
    void apply_configuration(
        const DockConfiguration &configuration);
    void set_monitor(
        const Glib::RefPtr<Gdk::Monitor>
            &monitor);
    void request_reveal();
    void schedule_show_tooltip(
        Gtk::Widget &item,
        const Glib::ustring &text);
    void schedule_show_preview(DockItem &item);
    void schedule_hide_tooltip(Gtk::Widget &item);
    void hide_tooltip_immediately();
    void dock_items_reordered();
    void dock_items_changed();
    void inhibit_autohide();
    void uninhibit_autohide(bool pointer_inside);
    void finish_autohide_drag(bool pointer_inside);
    void set_preview_rounded_corners(
        bool enabled,
        int radius);

    const DockSettings &settings() const
    {
        return m_settings;
    }

    const DockLayoutRequest &
    layout_request() const
    {
        return m_layout_request;
    }

    DockLocation location() const
    {
        return m_layout_request.location;
    }

    bool preview_input_forwarding() const;

private:
    bool sample_initial_x11_workarea();
    bool finish_initial_x11_placement();
    void update_dock_layout();
    void update_effective_icon_size(
        const MonitorGeometry &monitor,
        DockOrientation orientation);
    void schedule_layout_update();
    void schedule_icon_geometry_update();
    void update_icon_geometries();
    void schedule_intellihide_update();
    void update_intellihide();
    ScreenPosition dock_screen_position(
        bool prefer_surface_geometry,
        int requested_width = -1,
        int requested_height = -1) const;
    ScreenPosition calculated_dock_screen_position(
        int width,
        int height) const;
    void schedule_icon_refresh();
    void reload_icons();
    void hide_tooltip();
    void hide_preview(
        bool cancel_pending_show = true);
    void shell_preview_pointer_changed(bool inside);
    void activate_preview_window(
        const WindowId &window_id);
    void apply_thumbnail_policy();
    void start_hide_timer();
    void cancel_show_timer();
    void cancel_preview_show_timer();
    void cancel_hide_timer();

private:
    DockWindow &m_window;

    std::unique_ptr<DockAutohideController>
        m_autohide_controller;
    std::unique_ptr<LayoutCoordinator>
        m_layout_coordinator;
    std::unique_ptr<TooltipManager>
        m_tooltip_manager;
    std::unique_ptr<PreviewManager>
        m_preview_manager;

    Glib::RefPtr<Gdk::Monitor> m_monitor;
    Glib::RefPtr<Gtk::IconTheme> m_icon_theme;

    DockSettings m_settings;

    DockLayoutRequest m_layout_request;

    DockLayoutGeometry m_layout_geometry;

    DockLayoutEngine m_layout_engine;

    MonitorGeometry m_usable_monitor_geometry;
    MonitorGeometry m_output_geometry;

    DockPlacement m_placement;

    DockLocation m_applied_location =
        DockLocation::bottom;

    sigc::connection m_layout_update;
    sigc::connection m_icon_geometry_update;
    sigc::connection m_intellihide_update;
    sigc::connection m_edge_layout_update;
    sigc::connection m_icon_theme_changed;
    sigc::connection m_icon_refresh;
    sigc::connection m_realize;
    sigc::connection m_map;
    sigc::connection m_initial_x11_workarea_timer;
    sigc::connection m_initial_x11_placement_timer;
    sigc::connection m_size_allocate;
    sigc::connection m_window_registry_changed;
    sigc::connection
        m_window_registry_connection_changed;
    sigc::connection
        m_window_geometry_changed;
    sigc::connection
        m_dock_surface_geometry_changed;
    sigc::connection
        m_dock_workarea_geometry_changed;
    sigc::connection
        m_dock_reveal_requested;
    sigc::connection
        m_dock_pointer_inside_changed;
    sigc::connection
        m_preview_pointer_inside_changed;
    sigc::connection
        m_preview_input_forwarding_changed;
    sigc::connection
        m_preview_window_activated;
    sigc::connection
        m_dock_animation_completed;
    sigc::connection
        m_gnome_placement_fallback;
    sigc::connection m_dock_add;
    sigc::connection m_dock_remove;
    sigc::connection m_tooltip_will_show;
    sigc::connection m_tooltip_hide_requested;
    sigc::connection m_preview_pointer_entered;
    sigc::connection m_preview_pointer_left;

    bool m_initial_x11_workarea_pending = false;
    int m_initial_x11_workarea_stable_sample_count = 0;
    int m_initial_x11_workarea_sample_attempt_count = 0;
    MonitorGeometry m_initial_x11_output_sample;
    MonitorGeometry m_initial_x11_workarea_sample;
    ScreenPosition m_initial_x11_target_position;
    int m_initial_x11_placement_attempt_count = 0;

    bool m_backend_dock_pointer_state_known = false;
    bool m_backend_dock_pointer_inside = false;
    bool m_has_applied_layout = false;
};
