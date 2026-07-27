#pragma once

#include "dock_configuration.h"
#include "dock_layout_engine.h"
#include "dock_layout_geometry.h"

#include <gdkmm/monitor.h>
#include <gtkmm/icontheme.h>
#include <sigc++/connection.h>

class DockItem;
class DockWindow;

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
        DockItem &item);
    void schedule_hide_tooltip();

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
    void schedule_icon_refresh();
    void reload_icons();
    void show_tooltip(DockItem &item);
    void hide_tooltip();
    void start_hide_timer();
    void cancel_show_timer();
    void cancel_hide_timer();

private:
    DockWindow &m_window;

    Glib::RefPtr<Gdk::Monitor> m_monitor;
    Glib::RefPtr<Gtk::IconTheme> m_icon_theme;

    DockSettings m_settings;

    DockLayoutRequest m_layout_request;

    DockLayoutGeometry m_layout_geometry;

    DockLayoutEngine m_layout_engine;

    MonitorGeometry m_usable_monitor_geometry;

    sigc::connection m_show_timer;
    sigc::connection m_hide_timer;
    sigc::connection m_layout_update;
    sigc::connection m_icon_theme_changed;
    sigc::connection m_icon_refresh;
    sigc::connection m_realize;
    sigc::connection m_dock_add;
    sigc::connection m_dock_remove;

    DockItem *m_pending_item = nullptr;
};
