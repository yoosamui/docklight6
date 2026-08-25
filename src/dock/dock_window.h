// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window.h
//
// Purpose:
// Declares the main GTK dock window and its dock-item container.
//
// Responsibilities:
// - Own and synchronize launcher, running-app, and home widgets.
// - Apply logical orientation and visual styling.
// - Delegate calculated surface placement to DockSurfaceBackend.
// - Coordinate item ordering, attachment, drag-and-drop, and tooltips.
// - Provide the GTK surface used by DockWindowController.
//
// Dependencies and ownership:
// DockWindow owns its GTK children, tooltip, launcher manager, and
// controller. It borrows WindowRegistry and shares the selected monitor.
//
// Design notes:
// Layout calculations and timing policy are delegated to the controller
// and layout engine; native placement is delegated to DockSurfaceBackend.
//
// ------------------------------------------------------------

#pragma once

#include "config/dock_configuration.h"
#include "backends/dock_surface_backend.h"
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
class TooltipManager;
class PreviewManager;
class LayoutCoordinator;
class DockHomeItem;
struct DockRuntimeInfo;
class WindowRegistry;

class DockSurfaceBox : public Gtk::Box
{
public:
    DockSurfaceBox();
    void set_horizontal_scale(
        double scale,
        double anchor);
    double horizontal_scale() const;
    void set_vertical_scale(
        double scale,
        double anchor);
    double vertical_scale() const;
    void set_vertical_offset(double offset);
    double vertical_offset() const;

protected:
    bool on_draw(
        const Cairo::RefPtr<Cairo::Context>
            &context) override;

private:
    double m_horizontal_scale = 1.0;
    double m_horizontal_scale_anchor = 1.0;
    double m_vertical_scale = 1.0;
    double m_vertical_scale_anchor = 1.0;
    double m_vertical_offset = 0.0;
};

class DockWindow : public Gtk::Window
{
public:
    explicit DockWindow(
        const DockConfiguration &configuration,
        const Glib::RefPtr<Gdk::Monitor>
            &monitor,
        WindowRegistry *window_registry,
        const DockRuntimeInfo &runtime_info);
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
    void schedule_hide_tooltip(Gtk::Widget &item);
    void hide_tooltip_immediately();
    void inhibit_autohide();
    void uninhibit_autohide();
    void uninhibit_autohide(bool pointer_inside);
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
    DockAutohideEffect
    effective_autohide_effect() const;
    bool shows_x11_autohide_effects() const;

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
    void create_dock(
        const DockRuntimeInfo &runtime_info);
    void apply_dock_layout(
        const DockPlacement &placement,
        const MonitorGeometry &output,
        const MonitorGeometry &workarea);
    MonitorGeometry
    surface_output_geometry() const;
    MonitorGeometry
    surface_work_area() const;
    MonitorGeometry
    surface_effective_work_area(
        const MonitorGeometry &output,
        const MonitorGeometry &workarea);
    void set_surface_monitor(
        const Glib::RefPtr<Gdk::Monitor>
            &monitor);
    void prepare_surface_change();
    bool surface_uses_native_placement() const;
    bool surface_is_native_x11() const;
    bool surface_is_ordinary_wayland() const;
    DockAutohideEffect
    surface_default_autohide_effect() const;
    bool surface_delegates_autohide_effect(
        DockAutohideEffect effect) const;
    double surface_autohide_fade_opacity() const;
    void set_surface_autohide_fade_opacity(
        double opacity);
    void finish_surface_autohide_fade(
        bool hidden);
    bool surface_initial_placement_pending() const;
    void complete_surface_initial_placement();
    void apply_dock_orientation(
        DockOrientation orientation);
    void apply_visual_style();
    void set_x11_horizontal_scale(
        double scale,
        double anchor);
    double x11_horizontal_scale() const;
    void set_x11_vertical_scale(
        double scale,
        double anchor);
    double x11_vertical_scale() const;
    void set_x11_vertical_offset(double offset);
    double x11_vertical_offset() const;
    void apply_main_axis_end_margins(
        DockOrientation orientation);
    void register_dock_item(DockItem *item);
    void unregister_dock_item(DockItem *item);
    void synchronize_dock_items();
    void schedule_dock_item_sync();
    Glib::RefPtr<Gio::AppInfo>
    application_for_running(
        const std::string &desktop_id) const;
    const std::vector<DockItem *> &
    dock_items() const;
    DockWindowGeometry content_geometry() const;

private:
    friend class DockWindowController;
    friend class DockAutohideController;
    friend class TooltipManager;
    friend class PreviewManager;
    friend class LayoutCoordinator;

    Glib::RefPtr<Gtk::CssProvider> m_visual_css;

    DockSurfaceBox m_dock_box;
    // Authoritative typed view of the DockItem children. It is updated
    // before GTK add/remove signals fire and kept in visual order.
    std::vector<DockItem *>
        m_dock_items_cache;
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

    std::unique_ptr<IDockSurfaceBackend>
        m_surface_backend;
    std::unique_ptr<DockWindowController> m_controller;

    DockItem *m_dragged_item = nullptr;
    bool m_item_drop_accepted = false;

    bool m_has_synchronized_items = false;
};
