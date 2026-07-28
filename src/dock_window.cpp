#include "dock_window.h"

#include "dock_constants.h"
#include "dock_layout_metrics.h"
#include "dock_window_controller.h"
#include "launcher_manager.h"

#include <gtk-layer-shell.h>

#include <algorithm>
#include <memory>

DockWindow::DockWindow(
    const DockConfiguration &configuration,
    const Glib::RefPtr<Gdk::Monitor>
        &monitor,
    WindowRegistry *window_registry)
    : m_window_registry(window_registry)
{
    m_controller =
        std::make_unique<DockWindowController>(
            *this,
            configuration,
            monitor);

    set_decorated(false);
    set_resizable(false);
    set_app_paintable(true);

    // Rounded CSS corners expose pixels from the toplevel underneath the
    // dock box. Give that toplevel an alpha-capable visual so those pixels
    // remain transparent at both ends of the dock.
    auto screen = get_screen();

    if (screen)
    {
        auto rgba_visual =
            screen->get_rgba_visual();

        if (rgba_visual)
        {
            gtk_widget_set_visual(
                GTK_WIDGET(gobj()),
                rgba_visual->gobj());
        }
    }

    // Clear the complete layer surface before GTK paints the rounded dock
    // box. A transparent CSS background does not necessarily replace pixels
    // left behind when a mapped layer surface shrinks, which can make the
    // lower corners look square.
    signal_draw().connect(
        [](const Cairo::RefPtr<Cairo::Context> &context)
        {
            context->save();
            context->set_operator(
                Cairo::OPERATOR_SOURCE);
            context->set_source_rgba(
                0.0,
                0.0,
                0.0,
                0.0);
            context->paint();
            context->restore();

            return false;
        },
        false);

    GtkWindow *gtk_win =
        GTK_WINDOW(gobj());

    gtk_layer_init_for_window(gtk_win);

    gtk_layer_set_monitor(
        gtk_win,
        monitor
            ? monitor->gobj()
            : nullptr);

    m_overlay_window.set_monitor(
        monitor);

    gtk_layer_set_namespace(
        gtk_win,
        "docklight6");

    gtk_layer_set_layer(
        gtk_win,
        GTK_LAYER_SHELL_LAYER_TOP);

    get_style_context()->add_class(
        "dock-window");

    m_dock_box.get_style_context()
        ->add_class("dock-surface");

    m_visual_css =
        Gtk::CssProvider::create();

    m_dock_box.get_style_context()
        ->add_provider(
            m_visual_css,
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION +
                1);

    get_style_context()->add_provider(
        m_visual_css,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION +
            1);

    create_dock();

    m_effective_icon_size =
        std::max(
            1,
            m_controller
                ->settings()
                .icon_size());

    apply_visual_style();
    m_controller->initialize();
}

DockWindow::~DockWindow() = default;

void DockWindow::apply_configuration(
    const DockConfiguration &configuration)
{
    m_controller->apply_configuration(
        configuration);
}

void DockWindow::set_monitor(
    const Glib::RefPtr<Gdk::Monitor>
        &monitor)
{
    m_controller->set_monitor(monitor);
}

void DockWindow::schedule_show_tooltip(
    DockItem &item)
{
    m_controller->schedule_show_tooltip(
        item);
}

void DockWindow::schedule_hide_tooltip()
{
    m_controller->schedule_hide_tooltip();
}

void DockWindow::hide_tooltip_immediately()
{
    m_controller->hide_tooltip_immediately();
}

DockLocation DockWindow::location() const
{
    return m_controller->location();
}

DockWindowGeometry
DockWindow::content_geometry() const
{
    DockWindowGeometry geometry;

    const bool horizontal =
        m_dock_box.get_orientation() ==
        Gtk::ORIENTATION_HORIZONTAL;

    for (auto *child :
         m_dock_box.get_children())
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
            geometry.height =
                std::max(
                    geometry.height,
                    natural_height);
        }
        else
        {
            geometry.width =
                std::max(
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
    apply_dock_orientation(
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
}

void DockWindow::apply_dock_orientation(
    DockOrientation orientation)
{
    if (orientation ==
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
        orientation);
}

void DockWindow::apply_visual_style()
{
    auto dock_context =
        m_dock_box.get_style_context();

    auto window_context =
        get_style_context();

    const auto &layout_request =
        m_controller->layout_request();

    if (layout_request.rounded_corners)
    {
        dock_context->add_class(
            "dock-rounded");
        window_context->add_class(
            "dock-rounded");
    }
    else
    {
        dock_context->remove_class(
            "dock-rounded");
        window_context->remove_class(
            "dock-rounded");
    }

    const int configured_radius =
        layout_request.corner_radius;

    const int derived_radius =
        DockLayoutMetrics::corner_radius_for(
            m_effective_icon_size);

    const int effective_radius =
        layout_request.rounded_corners
            ? std::max(
                  0,
                  configured_radius < 0
                      ? derived_radius
                      : configured_radius)
            : 0;

    m_visual_css->load_from_data(
        "window.dock-window {"
        " background-color: transparent;"
        " border-radius: " +
        std::to_string(effective_radius) +
        "px;"
        "}"
        ".dock-surface {"
        " border-radius: " +
        std::to_string(effective_radius) +
        "px;"
        "}");

    for (auto *item : dock_items())
    {
        item->set_context_menu_corner_radius(
            effective_radius);
    }

    m_overlay_window.set_rounded_corners(
        layout_request.rounded_corners,
        effective_radius,
        m_effective_icon_size);
}

void DockWindow::apply_main_axis_end_margins(
    DockOrientation orientation)
{
    const bool horizontal =
        orientation ==
        DockOrientation::horizontal;

    const int leading_width =
        horizontal
            ? m_leading_main_axis_margin
            : 0;

    const int leading_height =
        horizontal
            ? 0
            : m_leading_main_axis_margin;

    const int trailing_width =
        horizontal
            ? m_trailing_main_axis_margin
            : 0;

    const int trailing_height =
        horizontal
            ? 0
            : m_trailing_main_axis_margin;

    // These spacers are children of m_dock_box, so GTK includes them in the
    // natural size used by DockLayoutEngine. This avoids a separate margin
    // calculation that could disagree with item and tooltip coordinates.
    m_leading_margin.set_size_request(
        leading_width,
        leading_height);

    m_trailing_margin.set_size_request(
        trailing_width,
        trailing_height);
}

std::vector<DockItem *>
DockWindow::dock_items()
{
    std::vector<DockItem *> items;

    for (auto *child :
         m_dock_box.get_children())
    {
        if (auto *item =
                dynamic_cast<DockItem *>(child))
        {
            items.push_back(item);
        }
    }

    return items;
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
                    launcher.app,
                    m_window_registry,
                    m_controller
                        ->settings()
                        .icon_size(),
                    m_controller
                        ->settings()
                        .hover_effect()));

        m_dock_box.pack_start(
            *item,
            Gtk::PACK_SHRINK);

        ++count;

        if (count >=
            DockConstants::MAX_DOCK_ITEMS)
        {
            break;
        }
    }

    m_dock_box.pack_start(
        m_trailing_margin,
        Gtk::PACK_SHRINK);

    add(m_dock_box);
    m_dock_box.show_all();
}
