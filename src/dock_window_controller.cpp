#include "dock_window_controller.h"

#include "dock_constants.h"
#include "dock_item.h"
#include "dock_layout_metrics.h"
#include "dock_window.h"

#include <gtk-layer-shell.h>
#include <gtkmm/settings.h>

#include <algorithm>

DockWindowController::DockWindowController(
    DockWindow &window,
    const DockConfiguration &configuration,
    const Glib::RefPtr<Gdk::Monitor>
        &monitor)
    : m_window(window),
      m_monitor(monitor),
      m_settings(configuration.settings),
      m_layout_request(
          configuration.layout_request)
{
}

DockWindowController::~DockWindowController()
{
    cancel_show_timer();
    cancel_hide_timer();
    m_layout_update.disconnect();
    m_icon_theme_changed.disconnect();
    m_icon_refresh.disconnect();
    m_realize.disconnect();
    m_dock_add.disconnect();
    m_dock_remove.disconnect();
}

void DockWindowController::initialize()
{
    // A launcher mutation changes the maximum size available to every item.
    // Coalesce GTK's add/remove notifications and recalculate after the
    // container has finished updating its child list.
    m_dock_add =
        m_window.m_dock_box.signal_add().connect(
            [this](Gtk::Widget *)
            {
                schedule_layout_update();
            });

    m_dock_remove =
        m_window.m_dock_box.signal_remove().connect(
            [this](Gtk::Widget *)
            {
                schedule_layout_update();
            });

    m_icon_theme =
        Gtk::IconTheme::get_default();

    if (m_icon_theme)
    {
        m_icon_theme_changed =
            m_icon_theme->signal_changed().connect(
                sigc::mem_fun(
                    *this,
                    &DockWindowController::
                        schedule_icon_refresh));

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
    m_realize =
        m_window.signal_realize().connect(
            sigc::mem_fun(
                *this,
                &DockWindowController::
                    update_dock_layout));

    update_dock_layout();
}

void DockWindowController::apply_configuration(
    const DockConfiguration &configuration)
{
    cancel_show_timer();
    cancel_hide_timer();
    m_pending_item = nullptr;
    hide_tooltip();

    m_settings =
        configuration.settings;

    for (auto *item : m_window.dock_items())
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

void DockWindowController::set_monitor(
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
            GTK_WINDOW(m_window.gobj()),
            m_monitor->gobj());

        m_window.m_overlay_window.set_monitor(
            m_monitor);
    }

    // The same monitor object can emit new geometry, work-area, or scale
    // values. Recalculate in both the move and geometry-change cases.
    schedule_layout_update();
}

void DockWindowController::update_dock_layout()
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
    m_window.apply_dock_orientation(
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
    auto dock_geometry =
        m_window.content_geometry();

    placement =
        m_layout_engine.calculate_dock_layout(
            m_layout_request,
            workarea_geometry,
            dock_geometry);

    apply_workarea_insets(
        placement,
        output_geometry,
        workarea_geometry);

    m_window.apply_dock_layout(placement);
}

void DockWindowController::apply_workarea_insets(
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

void DockWindowController::update_effective_icon_size(
    const MonitorGeometry &monitor,
    DockOrientation orientation)
{
    auto items =
        m_window.dock_items();

    const int previous_leading_margin =
        m_window.m_leading_main_axis_margin;

    const int previous_trailing_margin =
        m_window.m_trailing_main_axis_margin;

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

    m_window.m_leading_main_axis_margin =
        DockLayoutMetrics::DOCK_MARGIN;

    m_window.m_trailing_main_axis_margin =
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

        m_window.m_leading_main_axis_margin =
            remaining_length / 2;

        m_window.m_trailing_main_axis_margin =
            remaining_length -
            m_window.m_leading_main_axis_margin;
    }

    m_window.apply_main_axis_end_margins(
        orientation);

    const bool size_changed =
        effective_icon_size !=
        m_window.m_effective_icon_size;

    m_window.m_effective_icon_size =
        effective_icon_size;

    for (auto *item : items)
    {
        item->set_icon_size(
            m_window.m_effective_icon_size);
    }

    const bool margins_changed =
        previous_leading_margin !=
            m_window.m_leading_main_axis_margin ||
        previous_trailing_margin !=
            m_window.m_trailing_main_axis_margin;

    if (size_changed || margins_changed)
        m_window.apply_visual_style();
}

void DockWindowController::schedule_layout_update()
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

void DockWindowController::schedule_icon_refresh()
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

void DockWindowController::reload_icons()
{
    auto items =
        m_window.dock_items();

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

void DockWindowController::schedule_show_tooltip(
    DockItem &item)
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

void DockWindowController::schedule_hide_tooltip()
{
    cancel_show_timer();
    m_pending_item = nullptr;
    start_hide_timer();
}

void DockWindowController::show_tooltip(
    DockItem &item)
{
    const int tooltip_width =
        m_window.m_overlay_window
            .preferred_width_for(
                item.app_name());

    auto item_geometry =
        m_layout_geometry.item_geometry(
            item,
            m_window);

    auto dock_geometry =
        m_layout_geometry.dock_geometry(
            m_window);

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
            m_window.m_overlay_window
                .tooltip_height(),
            m_window.m_overlay_window
                .tooltip_distance());

    m_window.m_overlay_window.show_tooltip(
        item.app_name(),
        m_layout_request.location,
        tooltip_width,
        position);
}

void DockWindowController::hide_tooltip()
{
    m_window.m_overlay_window.hide_tooltip();
}

void DockWindowController::start_hide_timer()
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

void DockWindowController::cancel_show_timer()
{
    if (m_show_timer.connected())
        m_show_timer.disconnect();
}

void DockWindowController::cancel_hide_timer()
{
    if (m_hide_timer.connected())
        m_hide_timer.disconnect();
}
