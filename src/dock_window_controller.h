// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// dock_window_controller.h
//
// Purpose:
// Declares orchestration between configuration, geometry calculation,
// GTK lifecycle events, tooltip timing, and icon-geometry publication.
//
// Responsibilities:
// - Recalculate layout when configuration or monitor state changes.
// - Clamp effective icon size to the selected output.
// - Schedule tooltip, icon refresh, and geometry updates.
// - Keep DockWindow side effects ordered after GTK realization.
//
// Dependencies and ownership:
// The controller borrows its DockWindow, shares monitor and icon-theme
// references, and owns calculation objects and signal connections.
//
// Design notes:
// The controller is the boundary between pure layout rules and the
// event-driven GTK surface that applies those rules.
//
// ------------------------------------------------------------

#pragma once

#include "dock_configuration.h"
#include "dock_layout_engine.h"
#include "dock_layout_geometry.h"

#include <gdkmm/monitor.h>
#include <glibmm/ustring.h>
#include <gtkmm/icontheme.h>
#include <sigc++/connection.h>

#include <memory>

class DockWindow;
class DockAutohideController;

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
    void schedule_show_tooltip(
        Gtk::Widget &item,
        const Glib::ustring &text);
    void schedule_hide_tooltip();
    void hide_tooltip_immediately();
    void dock_items_reordered();
    void dock_items_changed();
    void inhibit_autohide();
    void uninhibit_autohide();
    void finish_autohide_drag(bool accepted);

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

private:
    void update_dock_layout();
    void apply_workarea_insets(
        DockPlacement &placement,
        const MonitorGeometry &output,
        const MonitorGeometry &workarea) const;
    void update_effective_icon_size(
        const MonitorGeometry &monitor,
        DockOrientation orientation);
    void schedule_layout_update();
    void schedule_icon_geometry_update();
    void update_icon_geometries();
    ScreenPosition dock_screen_position(
        bool prefer_surface_geometry) const;
    void schedule_icon_refresh();
    void reload_icons();
    void show_tooltip(
        Gtk::Widget &item,
        const Glib::ustring &text);
    void hide_tooltip();
    void start_hide_timer();
    void cancel_show_timer();
    void cancel_hide_timer();

private:
    DockWindow &m_window;

    std::unique_ptr<DockAutohideController>
        m_autohide_controller;

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

    sigc::connection m_show_timer;
    sigc::connection m_hide_timer;
    sigc::connection m_layout_update;
    sigc::connection m_icon_geometry_update;
    sigc::connection m_edge_layout_update;
    sigc::connection m_icon_theme_changed;
    sigc::connection m_icon_refresh;
    sigc::connection m_realize;
    sigc::connection m_size_allocate;
    sigc::connection m_window_registry_changed;
    sigc::connection
        m_dock_surface_geometry_changed;
    sigc::connection m_dock_add;
    sigc::connection m_dock_remove;

    Gtk::Widget *m_pending_item = nullptr;
    Glib::ustring m_pending_tooltip_text;

    bool m_has_applied_layout = false;
};
