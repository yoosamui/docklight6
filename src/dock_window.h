#pragma once

#include <gtkmm/box.h>
#include <gtkmm/cssprovider.h>
#include "dock_enums.h"
#include "dock_item.h"
#include "dock_tooltip_window.h"
#include "launcher_manager.h"
#include "dock_layout_geometry.h"
#include "dock_layout_engine.h"
#include <memory>
#include <sigc++/connection.h>

class DockTooltipWindow;

class DockWindow : public Gtk::Window
{
public:
    DockWindow();
    void schedule_show_tooltip(DockItem &item);
    void schedule_hide_tooltip();

private:
    void create_dock();
    void update_dock_layout();
    void apply_dock_layout(
        const DockPlacement &placement);
    void apply_visual_style();
    void apply_main_axis_end_margins(
        DockOrientation orientation);
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

    DockLayoutSettings m_layout_settings;
    DockLayoutGeometry m_layout_geometry;
    DockLayoutEngine m_layout_engine;
    DockTooltipWindow m_overlay_window;

    sigc::connection m_show_timer;
    sigc::connection m_hide_timer;
    DockItem *m_pending_item = nullptr;
};
