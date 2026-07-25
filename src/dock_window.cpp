#include "dock_window.h"
#include "dock_constants.h"
#include "dock_layout_metrics.h"
#include <gtk-layer-shell.h>

#include <algorithm>
#include <memory>

DockWindow::DockWindow(
    const DockConfiguration &configuration,
    const Glib::RefPtr<Gdk::Monitor>
        &monitor)
    : m_settings(configuration.settings),
      m_monitor(monitor),
      m_layout_request(
          configuration.layout_request)
{
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
            gtk_widget_set_visual(
                GTK_WIDGET(gobj()),
                rgba_visual->gobj());
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

    GtkWindow *gtk_win = GTK_WINDOW(gobj());

    gtk_layer_init_for_window(gtk_win);

    gtk_layer_set_monitor(
        gtk_win,
        m_monitor
            ? m_monitor->gobj()
            : nullptr);

    m_overlay_window.set_monitor(
        m_monitor);

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
    get_style_context()->add_provider(
        m_visual_css,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    // A launcher mutation changes the maximum size available to every item.
    // Coalesce GTK's add/remove notifications and recalculate after the
    // container has finished updating its child list.
    m_dock_box.signal_add().connect(
        [this](Gtk::Widget *)
        {
            schedule_layout_update();
        });

    m_dock_box.signal_remove().connect(
        [this](Gtk::Widget *)
        {
            schedule_layout_update();
        });

    create_dock();
    m_effective_icon_size =
        std::max(1, m_settings.icon_size());
    apply_visual_style();

    m_icon_theme =
        Gtk::IconTheme::get_default();

    if (m_icon_theme)
    {
        m_icon_theme_changed =
            m_icon_theme->signal_changed().connect(
                sigc::mem_fun(
                    *this,
                    &DockWindow::schedule_icon_refresh));

        auto gtk_settings =
            Gtk::Settings::get_default();

        if (gtk_settings)
        {
            const auto theme_name =
                gtk_settings
                    ->property_gtk_icon_theme_name()
                    .get_value();

            g_message(
                "Icon theme loaded: %s",
                theme_name.c_str());
        }
    }

    // At realize time the dock has a GDK window and can resolve its actual
    // monitor, but it has not mapped at an oversized natural size yet.
    signal_realize().connect(
        sigc::mem_fun(
            *this,
            &DockWindow::update_dock_layout));

    update_dock_layout();
}

DockWindow::~DockWindow()
{
    m_icon_theme_changed.disconnect();
    m_icon_refresh.disconnect();
}

void DockWindow::apply_configuration(
    const DockConfiguration &configuration)
{
    cancel_show_timer();
    cancel_hide_timer();
    m_pending_item = nullptr;
    hide_tooltip();

    m_settings =
        configuration.settings;

    for (auto *item : dock_items())
    {
        item->set_hover_effect(
            m_settings.hover_effect());
    }

    m_layout_request =
        configuration.layout_request;

    // Icon size, orientation, alignment, reservation, and visual settings all
    // converge in update_dock_layout(). Coalesce rapid configuration saves
    // into one recalculation on the GTK main loop.
    schedule_layout_update();
}

void DockWindow::set_monitor(
    const Glib::RefPtr<Gdk::Monitor>
        &monitor)
{
    if (!monitor)
        return;

    const bool monitor_changed =
        monitor != m_monitor;

    m_monitor = monitor;

    if (monitor_changed)
    {
        hide_tooltip();

        gtk_layer_set_monitor(
            GTK_WINDOW(gobj()),
            m_monitor->gobj());

        m_overlay_window.set_monitor(
            m_monitor);
    }

    // The same monitor object can emit new geometry, work-area, or scale
    // values. Recalculate in both the move and geometry-change cases.
    schedule_layout_update();
}

void DockWindow::update_dock_layout()
{
    auto output_geometry =
        m_layout_geometry.output_geometry(
            m_monitor);

    if (output_geometry.width <= 0 ||
        output_geometry.height <= 0)
    {
        // Never submit the unconstrained natural size to layer-shell. The
        // realize callback will retry once GDK can identify the output.
        return;
    }

    auto workarea_geometry =
        m_layout_geometry.monitor_geometry(
            m_monitor);

    if (workarea_geometry.width <= 0 ||
        workarea_geometry.height <= 0)
    {
        workarea_geometry =
            output_geometry;
    }

    const int reported_bottom_inset =
        std::max(
            0,
            output_geometry.y +
                output_geometry.height -
                workarea_geometry.y -
                workarea_geometry.height);

    // The compositor inset keeps the dock out of an occluded screen region.
    // DOCK_MARGIN is additional visible space between that region and the
    // dock; it must not be consumed as part of the occlusion workaround.
    const int required_bottom_inset =
        std::max(
            reported_bottom_inset,
            m_settings.minimum_bottom_workarea_inset()) +
        DockLayoutMetrics::DOCK_MARGIN;

    const int missing_bottom_inset =
        std::max(
            0,
            required_bottom_inset -
                reported_bottom_inset);

    workarea_geometry.height =
        std::max(
            1,
            workarea_geometry.height -
                missing_bottom_inset);

    m_usable_monitor_geometry = {
        workarea_geometry.x - output_geometry.x,
        workarea_geometry.y - output_geometry.y,
        workarea_geometry.width,
        workarea_geometry.height};

    // Apply orientation before measuring the dock. A vertical dock has a
    // different natural size than a horizontal one.
    auto placement =
        m_layout_engine.calculate_dock_layout(
            m_layout_request,
            workarea_geometry,
            {});

    // The first pass exists only to establish the box orientation. Do not
    // send its unknown (-1 x -1) size or partial anchor set to layer-shell:
    // switching from that temporary state to the final opposite-edge anchors
    // can make the compositor allocate the dock at its old natural length.
    apply_dock_orientation(
        placement.orientation);

    // DockItem::set_icon_size() and the spacer size requests update GTK's
    // preferred geometry synchronously. Measure and apply the final placement
    // now so the window cannot map once at its original oversized natural size.
    update_effective_icon_size(
        workarea_geometry,
        placement.orientation);

    // Measure each child directly. Gtk::Window can retain a cached preferred
    // size while orientation and spacer requests are changing, which clips a
    // trailing vertical margin after mapping.
    auto dock_geometry = content_geometry();

    placement =
        m_layout_engine.calculate_dock_layout(
            m_layout_request,
            workarea_geometry,
            dock_geometry);

    apply_workarea_insets(
        placement,
        output_geometry,
        workarea_geometry);

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

void DockWindow::apply_workarea_insets(
    DockPlacement &placement,
    const MonitorGeometry &output,
    const MonitorGeometry &workarea) const
{
    const int output_right =
        output.x + output.width;

    const int output_bottom =
        output.y + output.height;

    const int workarea_right =
        workarea.x + workarea.width;

    const int workarea_bottom =
        workarea.y + workarea.height;

    if (placement.is_horizontal())
    {
        placement.margin_left +=
            std::max(0, workarea.x - output.x);

        placement.margin_right +=
            std::max(0, output_right - workarea_right);
    }
    else
    {
        placement.margin_top +=
            std::max(0, workarea.y - output.y);

        placement.margin_bottom +=
            std::max(0, output_bottom - workarea_bottom);
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

    if (m_layout_request.rounded_corners)
    {
        dock_context->add_class("dock-rounded");
        window_context->add_class("dock-rounded");
    }
    else
    {
        dock_context->remove_class("dock-rounded");
        window_context->remove_class("dock-rounded");
    }

    const int configured_radius =
        m_layout_request.corner_radius;

    const int derived_radius =
        DockLayoutMetrics::corner_radius_for(
            m_effective_icon_size);

    const int effective_radius =
        m_layout_request.rounded_corners
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

    m_overlay_window.set_rounded_corners(
        m_layout_request.rounded_corners,
        effective_radius,
        m_effective_icon_size);
}

void DockWindow::apply_main_axis_end_margins(
    DockOrientation orientation)
{
    const bool horizontal =
        orientation == DockOrientation::horizontal;

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

void DockWindow::update_effective_icon_size(
    const MonitorGeometry &monitor,
    DockOrientation orientation)
{
    auto items = dock_items();

    const int previous_leading_margin =
        m_leading_main_axis_margin;

    const int previous_trailing_margin =
        m_trailing_main_axis_margin;

    const int requested_icon_size =
        std::max(1, m_settings.icon_size());

    int effective_icon_size =
        requested_icon_size;

    const int monitor_length =
        orientation == DockOrientation::horizontal
            ? monitor.width
            : monitor.height;

    if (!items.empty() && monitor_length > 0)
    {
        const int available_length =
            std::max(
                0,
                monitor_length -
                    2 * DockLayoutMetrics::DOCK_MARGIN);

        const int maximum_item_size =
            available_length /
            static_cast<int>(items.size());

        const int maximum_icon_size =
            std::max(
                1,
                maximum_item_size -
                    2 * DockLayoutMetrics::DOCK_ITEM_PADDING);

        effective_icon_size =
            std::min(
                requested_icon_size,
                maximum_icon_size);
    }

    m_leading_main_axis_margin =
        DockLayoutMetrics::DOCK_MARGIN;

    m_trailing_main_axis_margin =
        DockLayoutMetrics::DOCK_MARGIN;

    const bool constrained =
        effective_icon_size < requested_icon_size;

    if (constrained && !items.empty())
    {
        const int items_length =
            static_cast<int>(items.size()) *
            DockLayoutMetrics::item_size_for(
                effective_icon_size);

        const int remaining_length =
            std::max(
                0,
                monitor_length - items_length);

        m_leading_main_axis_margin =
            remaining_length / 2;

        m_trailing_main_axis_margin =
            remaining_length -
            m_leading_main_axis_margin;
    }

    apply_main_axis_end_margins(orientation);

    const bool size_changed =
        effective_icon_size != m_effective_icon_size;

    m_effective_icon_size =
        effective_icon_size;

    for (auto *item : items)
        item->set_icon_size(
            m_effective_icon_size);

    const bool margins_changed =
        previous_leading_margin !=
            m_leading_main_axis_margin ||
        previous_trailing_margin !=
            m_trailing_main_axis_margin;

    if (size_changed || margins_changed)
        apply_visual_style();
}

void DockWindow::schedule_layout_update()
{
    if (m_layout_update.connected())
        m_layout_update.disconnect();

    m_layout_update =
        Glib::signal_idle().connect(
            [this]()
            {
                update_dock_layout();
                return false;
            });
}

void DockWindow::schedule_icon_refresh()
{
    if (m_icon_refresh.connected())
        return;

    // A desktop theme switch can invalidate several icon-theme caches in
    // quick succession. Refresh all dock items once after GTK has processed
    // the complete change.
    m_icon_refresh =
        Glib::signal_idle().connect(
            [this]()
            {
                reload_icons();
                return false;
            });
}

void DockWindow::reload_icons()
{
    auto items = dock_items();

    for (auto *item : items)
        item->reload_icon();

    auto gtk_settings =
        Gtk::Settings::get_default();

    if (gtk_settings)
    {
        const auto theme_name =
            gtk_settings
                ->property_gtk_icon_theme_name()
                .get_value();

        g_message(
            "Icon theme reloaded: %s (%zu icons)",
            theme_name.c_str(),
            items.size());
    }
    else
    {
        g_message(
            "Icon theme reloaded: %zu icons",
            items.size());
    }
}

std::vector<DockItem *> DockWindow::dock_items()
{
    std::vector<DockItem *> items;

    for (auto *child : m_dock_box.get_children())
    {
        if (auto *item = dynamic_cast<DockItem *>(child))
            items.push_back(item);
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
                    m_settings.icon_size(),
                    m_settings.hover_effect()));

        m_dock_box.pack_start(
            *item,
            Gtk::PACK_SHRINK);

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
        m_usable_monitor_geometry;

    if (monitor_geometry.width <= 0 ||
        monitor_geometry.height <= 0)
    {
        monitor_geometry =
            m_layout_geometry.output_geometry(
                m_monitor);

        // Layer-shell margins are relative to the selected output, not the
        // global desktop coordinate space.
        monitor_geometry.x = 0;
        monitor_geometry.y = 0;
    }

    auto position =
        m_layout_engine.calculate_tooltip_position(
            m_layout_request,
            monitor_geometry,
            dock_geometry,
            item_geometry,
            tooltip_width,
            m_overlay_window.tooltip_height(),
            m_overlay_window.tooltip_distance());

    m_overlay_window.show_tooltip(
        item.app_name(),
        m_layout_request.location,
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
