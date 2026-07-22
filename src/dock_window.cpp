#include "dock_window.h"
#include "dock_constants.h"
#include "dock_layout_metrics.h"
#include "dock_settings.h"
#include <gtk-layer-shell.h>

#include <algorithm>
#include <iostream>
#include <memory>

DockWindow::DockWindow()
{

    std::cout
        << "DockWindow constructor visible="
        << get_visible()
        << std::endl;

    set_decorated(false);
    set_resizable(false);

    GtkWindow *gtk_win = GTK_WINDOW(gobj());

    gtk_layer_init_for_window(gtk_win);

    gtk_layer_set_namespace(
        gtk_win,
        "docklight6");

    gtk_layer_set_layer(
        gtk_win,
        GTK_LAYER_SHELL_LAYER_TOP);

    get_style_context()->add_class("dock-window");
    m_dock_box.get_style_context()->add_class("dock-surface");

    m_visual_css = Gtk::CssProvider::create();
    m_dock_box.get_style_context()->add_provider(
        m_visual_css,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    create_dock();
    apply_visual_style();

    signal_map().connect(
        sigc::mem_fun(
            *this,
            &DockWindow::update_dock_layout));

    update_dock_layout();
}

void DockWindow::update_dock_layout()
{
    auto monitor_geometry =
        m_layout_geometry.output_geometry(
            *this);

    // Apply orientation before measuring the dock. A vertical dock has a
    // different natural size than a horizontal one.
    auto placement =
        m_layout_engine.calculate_dock_layout(
            m_layout_settings,
            monitor_geometry,
            {});

    apply_dock_layout(placement);

    // Measure each child directly. Gtk::Window can retain a cached preferred
    // size while orientation and spacer requests are changing, which clips a
    // trailing vertical margin after mapping.
    auto dock_geometry = content_geometry();

    placement =
        m_layout_engine.calculate_dock_layout(
            m_layout_settings,
            monitor_geometry,
            dock_geometry);

    apply_dock_layout(placement);
}

DockWindowGeometry DockWindow::content_geometry() const
{
    DockWindowGeometry geometry;

    const bool horizontal =
        m_dock_box.get_orientation() ==
        Gtk::ORIENTATION_HORIZONTAL;

    for (auto *child : m_dock_box.get_children())
    {
        int minimum_width = 0;
        int natural_width = 0;
        int minimum_height = 0;
        int natural_height = 0;

        child->get_preferred_width(
            minimum_width,
            natural_width);

        child->get_preferred_height(
            minimum_height,
            natural_height);

        if (horizontal)
        {
            geometry.width += natural_width;
            geometry.height = std::max(
                geometry.height,
                natural_height);
        }
        else
        {
            geometry.width = std::max(
                geometry.width,
                natural_width);
            geometry.height += natural_height;
        }
    }

    return geometry;
}

void DockWindow::apply_dock_layout(
    const DockPlacement &placement)
{

    apply_visual_style();

    if (placement.orientation ==
        DockOrientation::vertical)
    {
        m_dock_box.set_orientation(
            Gtk::ORIENTATION_VERTICAL);
    }
    else
    {
        m_dock_box.set_orientation(
            Gtk::ORIENTATION_HORIZONTAL);
    }

    apply_main_axis_end_margins(
        placement.orientation);

    GtkWindow *gtk_win =
        GTK_WINDOW(gobj());

    gtk_layer_set_anchor(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_LEFT,
        placement.anchor_left);

    gtk_layer_set_anchor(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        placement.anchor_right);

    gtk_layer_set_anchor(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_TOP,
        placement.anchor_top);

    gtk_layer_set_anchor(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        placement.anchor_bottom);

    gtk_layer_set_margin(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_LEFT,
        placement.margin_left);

    gtk_layer_set_margin(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        placement.margin_right);

    gtk_layer_set_margin(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_TOP,
        placement.margin_top);

    gtk_layer_set_margin(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        placement.margin_bottom);

    // gtk-layer-shell uses the GTK widget request as the surface's natural
    // size. set_default_size() alone does not reliably resize an already
    // mapped layer surface, particularly on the vertical main axis.
    gtk_widget_set_size_request(
        GTK_WIDGET(gtk_win),
        placement.width,
        placement.height);

    // Request a fresh configure after changing the size request. The actual
    // size remains compositor-controlled when opposite anchors are active.
    gtk_window_resize(gtk_win, 1, 1);

    if (placement.exclusive_zone < 0)
    {
        gtk_layer_auto_exclusive_zone_enable(
            gtk_win);
    }
    else
    {
        gtk_layer_set_exclusive_zone(
            gtk_win,
            placement.exclusive_zone);
    }

    std::cout
        << "Dock reservation: zone="
        << gtk_layer_get_exclusive_zone(gtk_win)
        << " auto="
        << gtk_layer_auto_exclusive_zone_is_enabled(gtk_win)
        << " anchors="
        << placement.anchor_left
        << placement.anchor_right
        << placement.anchor_top
        << placement.anchor_bottom
        << std::endl;
}

void DockWindow::apply_visual_style()
{
    auto context = m_dock_box.get_style_context();

    if (m_layout_settings.rounded_corners)
        context->add_class("dock-rounded");
    else
        context->remove_class("dock-rounded");

    const int configured_radius =
        m_layout_settings.corner_radius;

    const int derived_radius =
        DockLayoutMetrics::corner_radius_for(
            g_settings.icon_size());

    const int effective_radius =
        m_layout_settings.rounded_corners
            ? std::max(
                  0,
                  configured_radius < 0
                      ? derived_radius
                      : configured_radius)
            : 0;

    m_visual_css->load_from_data(
        ".dock-surface { border-radius: " +
        std::to_string(effective_radius) +
        "px; }");

    m_overlay_window.set_rounded_corners(
        m_layout_settings.rounded_corners,
        effective_radius,
        g_settings.icon_size());
}

void DockWindow::apply_main_axis_end_margins(
    DockOrientation orientation)
{
    const bool horizontal =
        orientation == DockOrientation::horizontal;

    const int main_axis_margin =
        DockLayoutMetrics::DOCK_MARGIN;

    const int width = horizontal
                          ? main_axis_margin
                          : 0;

    const int height = horizontal
                           ? 0
                           : main_axis_margin;

    // These spacers are children of m_dock_box, so GTK includes them in the
    // natural size used by DockLayoutEngine. This avoids a separate margin
    // calculation that could disagree with item and tooltip coordinates.
    m_leading_margin.set_size_request(width, height);
    m_trailing_margin.set_size_request(width, height);
}

void DockWindow::create_dock()
{
    LauncherManager manager;

    auto apps =
        manager.load_applications();

    int count = 0;

    m_dock_box.pack_start(
        m_leading_margin,
        Gtk::PACK_SHRINK);

    for (const auto &launcher : apps)
    {
        auto item =
            Gtk::manage(
                new DockItem(
                    *this,
                    launcher.app));

        m_dock_box.pack_start(
            *item,
            Gtk::PACK_SHRINK);

        std::cout
            << "Created DockItem"
            << std::endl;

        ++count;

        if (count >= DockConstants::MAX_DOCK_ITEMS)
            break;
    }

    m_dock_box.pack_start(
        m_trailing_margin,
        Gtk::PACK_SHRINK);

    add(m_dock_box);
    m_dock_box.show_all();
}

void DockWindow::schedule_show_tooltip(DockItem &item)
{
    cancel_hide_timer();
    cancel_show_timer();

    m_pending_item = &item;

    m_show_timer =
        Glib::signal_timeout().connect(
            [this]()
            {
                if (m_pending_item)
                    show_tooltip(*m_pending_item);

                m_pending_item = nullptr;
                return false;
            },
            DockConstants::TOOLTIP_SHOW_DELAY_MS);
}

void DockWindow::schedule_hide_tooltip()
{
    cancel_show_timer();
    m_pending_item = nullptr;
    start_hide_timer();
}

void DockWindow::show_tooltip(DockItem &item)
{
    const int tooltip_width =
        m_overlay_window.preferred_width_for(
            item.app_name());

    auto item_geometry =
        m_layout_geometry.item_geometry(
            item,
            *this);

    auto dock_geometry =
        m_layout_geometry.dock_geometry(
            *this);

    auto monitor_geometry =
        m_layout_geometry.output_geometry(
            *this);

    auto position =
        m_layout_engine.calculate_tooltip_position(
            m_layout_settings,
            monitor_geometry,
            dock_geometry,
            item_geometry,
            tooltip_width,
            m_overlay_window.tooltip_height(),
            m_overlay_window.tooltip_distance());

    m_overlay_window.show_tooltip(
        item.app_name(),
        m_layout_settings.location,
        tooltip_width,
        position);
}

void DockWindow::hide_tooltip()
{
    m_overlay_window.hide_tooltip();
}

void DockWindow::start_hide_timer()
{
    cancel_hide_timer();

    m_hide_timer =
        Glib::signal_timeout().connect(
            [this]()
            {
                hide_tooltip();
                return false;
            },
            DockConstants::TOOLTIP_HIDE_DELAY_MS);
}

void DockWindow::cancel_show_timer()
{
    if (m_show_timer.connected())
        m_show_timer.disconnect();
}

void DockWindow::cancel_hide_timer()
{
    if (m_hide_timer.connected())
        m_hide_timer.disconnect();
}
