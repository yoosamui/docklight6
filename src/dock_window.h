#pragma once

#include "dock_configuration.h"
#include "dock_item.h"
#include "dock_layout_metrics.h"
#include "dock_layout_types.h"
#include "dock_tooltip_window.h"
#include "dock_window_geometry.h"

#include <gdkmm/monitor.h>
#include <gtkmm/box.h>
#include <gtkmm/cssprovider.h>

#include <memory>
#include <vector>

class DockWindowController;

class DockWindow : public Gtk::Window
{
public:
    explicit DockWindow(
        const DockConfiguration &configuration,
        const Glib::RefPtr<Gdk::Monitor>
            &monitor);
    ~DockWindow() override;
    void apply_configuration(
        const DockConfiguration &configuration);
    void set_monitor(
        const Glib::RefPtr<Gdk::Monitor>
            &monitor);
    void schedule_show_tooltip(DockItem &item);
    void schedule_hide_tooltip();
    DockLocation location() const;

private:
    void create_dock();
    void apply_dock_layout(
        const DockPlacement &placement);
    void apply_dock_orientation(
        DockOrientation orientation);
    void apply_visual_style();
    void apply_main_axis_end_margins(
        DockOrientation orientation);
    std::vector<DockItem *> dock_items();
    DockWindowGeometry content_geometry() const;

private:
    friend class DockWindowController;

    Glib::RefPtr<Gtk::CssProvider> m_visual_css;

    Gtk::Box m_dock_box{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Box m_leading_margin;
    Gtk::Box m_trailing_margin;

    // Visible empty widgets whose requested size becomes the dock's leading
    // and trailing content margin on the active orientation axis.
    int m_leading_main_axis_margin =
        DockLayoutMetrics::DOCK_MARGIN;
    int m_trailing_main_axis_margin =
        DockLayoutMetrics::DOCK_MARGIN;

    // The configured icon size is a request. This effective size is clamped
    // so every item and both end margins fit on the monitor's main axis.
    int m_effective_icon_size = 0;

    DockTooltipWindow m_overlay_window;

    std::unique_ptr<DockWindowController> m_controller;
};
