// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window.h
//
// Purpose:
// Declares the main GTK layer-shell surface and its dock-item container.
//
// Responsibilities:
// - Own and synchronize launcher, running-app, and home widgets.
// - Apply calculated dock placement, orientation, and visual styling.
// - Coordinate item ordering, attachment, drag-and-drop, and tooltips.
// - Provide the GTK surface used by DockWindowController.
//
// Dependencies and ownership:
// DockWindow owns its GTK children, tooltip, launcher manager, and
// controller. It borrows WindowRegistry and shares the selected monitor.
//
// Design notes:
// Layout calculations and timing policy are delegated to the controller
// and layout engine; this class applies results to GTK and layer-shell.
//
// ------------------------------------------------------------

#pragma once

#include "config/dock_configuration.h"
#include "dock_item.h"
#include "layout/dock_layout_metrics.h"
#include "layout/dock_layout_types.h"
#include "dock_tooltip_window.h"
#include "layout/dock_window_geometry.h"
#include "launchers/launcher_manager.h"

#include <gdkmm/monitor.h>
#include <gtkmm/box.h>
#include <gtkmm/cssprovider.h>

#include <memory>
#include <vector>

class DockWindowController;
class DockAutohideController;
class DockHomeItem;
class WindowRegistry;

class DockWindow : public Gtk::Window
{
public:
    explicit DockWindow(
        const DockConfiguration &configuration,
        const Glib::RefPtr<Gdk::Monitor>
            &monitor,
        WindowRegistry *window_registry);
    ~DockWindow() override;
    void apply_configuration(
        const DockConfiguration &configuration);
    void set_monitor(
        const Glib::RefPtr<Gdk::Monitor>
            &monitor);
    void request_reveal();
    void schedule_show_tooltip(DockItem &item);
    void schedule_show_tooltip(
        Gtk::Widget &item,
        const Glib::ustring &text);
    void schedule_hide_tooltip();
    void hide_tooltip_immediately();
    void inhibit_autohide();
    void uninhibit_autohide();
    bool set_item_attached(
        DockItem &item,
        bool attached);
    void begin_item_drag(DockItem &item);
    bool can_drop_item(
        const DockItem &target);
    bool drop_item(
        DockItem &target,
        int x,
        int y);
    void end_item_drag(DockItem &item);
    DockLocation location() const;
    bool preview_input_forwarding() const;

protected:
    bool on_drag_motion(
        const Glib::RefPtr<
            Gdk::DragContext> &context,
        int x,
        int y,
        guint time) override;
    bool on_drag_drop(
        const Glib::RefPtr<
            Gdk::DragContext> &context,
        int x,
        int y,
        guint time) override;

private:
    bool is_first_item_drop_zone(
        int x,
        int y);
    bool drop_item_first();
    bool pointer_is_inside();
    bool apply_dragged_item_order(
        const std::vector<
            DockItem *> &items);
    void create_dock();
    void apply_dock_layout(
        const DockPlacement &placement,
        const MonitorGeometry &output,
        const MonitorGeometry &workarea);
    void apply_x11_strut(
        const DockPlacement &placement,
        int x,
        int y,
        int width,
        int height);
    void prepare_x11_monitor_change();
    void capture_x11_base_workarea(
        const MonitorGeometry &output,
        const MonitorGeometry &fallback);
    void apply_dock_orientation(
        DockOrientation orientation);
    void apply_visual_style();
    void apply_main_axis_end_margins(
        DockOrientation orientation);
    void synchronize_dock_items();
    void schedule_dock_item_sync();
    Glib::RefPtr<Gio::AppInfo>
    application_for_running(
        const std::string &desktop_id) const;
    std::vector<DockItem *> dock_items();
    DockWindowGeometry content_geometry() const;

private:
    friend class DockWindowController;
    friend class DockAutohideController;

    Glib::RefPtr<Gtk::CssProvider> m_visual_css;

    Gtk::Box m_dock_box{Gtk::ORIENTATION_HORIZONTAL};
    Gtk::Box m_leading_margin;
    Gtk::Box m_trailing_margin;

    DockHomeItem *m_home_item = nullptr;

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

    WindowRegistry *m_window_registry =
        nullptr;

    LauncherManager m_launcher_manager;
    sigc::connection m_dock_item_sync;

    std::vector<std::string>
        m_synchronized_attached_ids;
    std::vector<std::string>
        m_synchronized_running_ids;

    std::unique_ptr<DockWindowController> m_controller;

    DockItem *m_dragged_item = nullptr;
    bool m_item_drop_accepted = false;

    bool m_has_synchronized_items = false;
    bool m_uses_layer_shell = false;
    bool m_initial_gnome_placement_pending = false;
    bool m_has_x11_base_workarea = false;
    MonitorGeometry m_x11_base_workarea;
};
