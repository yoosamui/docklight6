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

#include <cairomm/surface.h>
#include <gdkmm/monitor.h>
#include <gdkmm/pixbuf.h>
#include <gdkmm/frameclock.h>
#include <gtkmm/box.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/alignment.h>
#include <gtkmm/overlay.h>

#include <memory>
#include <utility>
#include <vector>

class DockWindowController;
class DockAutohideController;
class LayerShellDockSurfaceBackend;
class LegacyDockSurfaceBackend;
class TooltipManager;
class PreviewManager;
class LayoutCoordinator;
class DockHomeItem;
class DockSessionItem;
struct DockRuntimeInfo;
class WindowRegistry;

struct DockMagnifiedIcon
{
    Glib::RefPtr<Gdk::Pixbuf> pixbuf;
    double x = 0.0;
    double y = 0.0;
    DockIndicatorVisual indicator;
    int indicator_x = 0;
    int indicator_y = 0;
    int indicator_width = 0;
    int indicator_height = 0;
};

class DockMagnifiedLayer final
    : public Gtk::DrawingArea
{
public:
    DockMagnifiedLayer();

    void set_icons(
        std::vector<DockMagnifiedIcon> icons);
    void paint(
        const Cairo::RefPtr<Cairo::Context>
            &context) const;

protected:
    bool on_draw(
        const Cairo::RefPtr<Cairo::Context>
            &context) override;

private:
    std::vector<DockMagnifiedIcon> m_icons;
};

class DockSurfaceBox : public Gtk::Box
{
public:
    DockSurfaceBox();
    void set_external_background(bool external);
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
    void set_horizontal_offset(double offset);
    double horizontal_offset() const;

protected:
    bool on_draw(
        const Cairo::RefPtr<Cairo::Context>
            &context) override;

private:
    bool m_external_background = false;
    double m_horizontal_scale = 1.0;
    double m_horizontal_scale_anchor = 1.0;
    double m_vertical_scale = 1.0;
    double m_vertical_scale_anchor = 1.0;
    double m_vertical_offset = 0.0;
    double m_horizontal_offset = 0.0;
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
    void update_magnified_hover(int x, int y);
    void reset_magnified_hover();
    void set_magnified_enabled(bool enabled);
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
    // The launcher store also holds saved Sessions, so the Session dialog
    // reaches it through the dock rather than opening the file itself.
    LauncherManager &launcher_manager();
    // Rebuilds the dock after the Session editor changes a Session, so a new
    // or edited Session appears without waiting for a window event.
    void synchronize_session_items();
    void edit_session(
        const std::string &session_name);
    bool preview_input_forwarding() const;
    DockAutohideEffect
    effective_autohide_effect() const;
    std::vector<DockAutohideEffect>
    configurable_autohide_effects() const;

protected:
    bool on_draw(
        const Cairo::RefPtr<Cairo::Context>
            &context) override;
    bool on_leave_notify_event(
        GdkEventCrossing *event) override;
    bool on_motion_notify_event(
        GdkEventMotion *event) override;
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
    bool point_is_over_dock_body(
        int x,
        int y);
    bool pointer_is_over_dock_body();
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
    bool surface_delegates_autohide_effect(
        DockAutohideEffect effect) const;
    double surface_autohide_fade_opacity() const;
    void set_surface_autohide_fade_opacity(
        double opacity);
    void finish_surface_autohide_fade(
        bool hidden);
    bool surface_supports_autohide_slide() const;
    double surface_autohide_slide_progress() const;
    void set_surface_autohide_slide_progress(
        const DockPlacement &placement,
        double progress);
    void finish_surface_autohide_slide(
        bool hidden);
    bool surface_initial_placement_pending() const;
    void complete_surface_initial_placement();
    void apply_dock_orientation(
        DockOrientation orientation);
    void set_magnified_layer_active(
        bool active);
    void release_magnified_hover();
    void clear_magnified_hover_frame();
    void set_magnified_main_axis_overflow(
        double margin_extra);
    bool advance_magnified_hover();
    void apply_magnified_visual_center(
        Gtk::Widget &item,
        ItemGeometry &geometry) const;
    int magnified_surface_cross_axis_size() const;
    int magnified_main_axis_extra_size() const;
    int normal_dock_cross_axis_size() const;
    bool magnified_surface_enabled() const;
    void apply_visual_style();
    void set_x11_horizontal_scale(
        double scale,
        double anchor);
    double x11_horizontal_scale() const;
    void set_x11_vertical_scale(
        double scale,
        double anchor);
    double x11_vertical_scale() const;
    ScreenPosition x11_autohide_slide_content_offset(
        const DockPlacement &placement) const;
    void set_x11_horizontal_offset(double offset);
    double x11_horizontal_offset() const;
    void set_x11_vertical_offset(double offset);
    double x11_vertical_offset() const;
    void set_surface_horizontal_offset(double offset);
    void set_surface_vertical_offset(double offset);
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
    friend class LayerShellDockSurfaceBackend;
    friend class LegacyDockSurfaceBackend;
    friend class TooltipManager;
    friend class PreviewManager;
    friend class LayoutCoordinator;

    Glib::RefPtr<Gtk::CssProvider> m_visual_css;

    DockSurfaceBox m_dock_box;
    Gtk::Alignment m_dock_alignment;
    Gtk::Overlay m_dock_overlay;
    DockMagnifiedLayer m_magnified_layer;
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
    int m_applied_leading_margin_width = -2;
    int m_applied_leading_margin_height = -2;
    int m_applied_trailing_margin_width = -2;
    int m_applied_trailing_margin_height = -2;

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
    std::vector<std::string>
        m_synchronized_session_ids;
    std::vector<std::string>
        m_synchronized_dock_order;

    std::unique_ptr<IDockSurfaceBackend>
        m_surface_backend;
    std::unique_ptr<DockWindowController> m_controller;

    DockItem *m_dragged_item = nullptr;
    bool m_item_drop_accepted = false;
    std::vector<std::pair<DockItem *, double>>
        m_magnified_anchors;
    std::vector<std::pair<Gtk::Widget *, double>>
        m_magnified_visual_centers;
    double m_home_magnified_anchor = 0.0;
    bool m_magnified_pointer_active = false;
    bool m_magnified_x11_buffered = false;
    double m_magnified_painted_margin_extra = 0.0;
    Cairo::RefPtr<Cairo::ImageSurface> m_magnified_x11_frame;
    int m_magnified_x11_frame_scale = 0;
    int m_magnified_pointer_x = 0;
    int m_magnified_pointer_y = 0;
    int m_magnified_last_rendered_pointer_x = 0;
    int m_magnified_last_rendered_pointer_y = 0;
    bool m_magnified_frame_initialized = false;
    bool m_magnified_releasing = false;
    // Per-end styled extension currently exchanged from the fixed transparent
    // main-axis surface capacity.
    int m_magnified_main_axis_margin_extra = 0;
    guint m_magnified_tick_callback = 0;
    gint64 m_magnified_frame_time_us = 0;
    bool m_magnified_enabled = false;

    bool m_has_synchronized_items = false;
};
