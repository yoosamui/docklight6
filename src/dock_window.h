#pragma once

#include <gtkmm/box.h>
#include <gtkmm/cssprovider.h>
#include "dock_configuration.h"
#include "dock_layout_metrics.h"
#include "dock_layout_types.h"
#include "dock_item.h"
#include "dock_tooltip_window.h"
#include "launcher_manager.h"
#include "dock_layout_geometry.h"
#include "dock_layout_engine.h"
#include <memory>
#include <sigc++/connection.h>
#include <vector>

class DockTooltipWindow;

class DockWindow : public Gtk::Window
{
public:
    explicit DockWindow(
        const DockConfiguration &configuration);
    void apply_configuration(
        const DockConfiguration &configuration);
    void schedule_show_tooltip(DockItem &item);
    void schedule_hide_tooltip();

private:
    void create_dock();
    void update_dock_layout();
    void apply_dock_layout(
        const DockPlacement &placement);
    void apply_workarea_insets(
        DockPlacement &placement,
        const MonitorGeometry &output,
        const MonitorGeometry &workarea) const;
    void apply_dock_orientation(
        DockOrientation orientation);
    void apply_visual_style();
    void apply_main_axis_end_margins(
        DockOrientation orientation);
    void update_effective_icon_size(
        const MonitorGeometry &monitor,
        DockOrientation orientation);
    void schedule_layout_update();
    std::vector<DockItem *> dock_items();
    DockWindowGeometry content_geometry() const;
    void show_tooltip(DockItem &item);
    void hide_tooltip();
    void start_hide_timer();
    void cancel_show_timer();
    void cancel_hide_timer();

private:
    std::unique_ptr<DockTooltipWindow> m_tooltip_window;

    Gtk::Box m_dock_box{
        Gtk::ORIENTATION_HORIZONTAL};
    Glib::RefPtr<Gtk::CssProvider> m_visual_css;

    // Visible empty widgets whose requested size becomes the dock's leading
    // and trailing content margin on the active orientation axis.
    Gtk::Box m_leading_margin;
    Gtk::Box m_trailing_margin;
    int m_leading_main_axis_margin =
        DockLayoutMetrics::DOCK_MARGIN;
    int m_trailing_main_axis_margin =
        DockLayoutMetrics::DOCK_MARGIN;
    // The configured icon size is a request. This effective size is clamped
    // so every item and both end margins fit on the monitor's main axis.
    int m_effective_icon_size = 0;

    DockSettings m_settings;
    DockLayoutRequest m_layout_request;
    DockLayoutGeometry m_layout_geometry;
    DockLayoutEngine m_layout_engine;
    // Output-relative usable geometry from the latest dock layout. Tooltips
    // must use this same area or asymmetric screen insets shift their centre.
    MonitorGeometry m_usable_monitor_geometry;
    DockTooltipWindow m_overlay_window;

    sigc::connection m_show_timer;
    sigc::connection m_hide_timer;
    sigc::connection m_layout_update;
    DockItem *m_pending_item = nullptr;
};
