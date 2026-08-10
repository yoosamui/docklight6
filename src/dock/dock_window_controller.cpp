// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window_controller.cpp
//
// Implementation overview:
// Coordinates dock layout, work-area adjustments, tooltip scheduling,
// icon refreshes, and publication of compositor effect geometry.
//
// Important implementation decisions:
// - Expensive GTK reactions are coalesced through idle callbacks.
// - Pure placement is calculated before DockWindow applies side effects.
// - Effective icon size is derived from available monitor space.
// - Published icon geometry prefers compositor surface coordinates.
// - Timers enforce tooltip intent without embedding timing in widgets.
//
// ------------------------------------------------------------

#include "dock_window_controller.h"

#include "autohide/dock_autohide_controller.h"
#include "dock_constants.h"
#include "dock_home_item.h"
#include "autohide/dock_intellihide_policy.h"
#include "dock_item.h"
#include "layout/dock_layout_metrics.h"
#include "media/dock_media_playback_monitor.h"
#include "preview/dock_preview_window.h"
#include "dock_window.h"
#include "windowing/window_icon_geometry.h"
#include "windowing/window_registry.h"

#include <gdk/gdkwayland.h>
#include <gtk-layer-shell.h>
#include <gtkmm/settings.h>

#include <algorithm>

DockWindowController::DockWindowController(
    DockWindow &window,
    const DockConfiguration &configuration,
    const Glib::RefPtr<Gdk::Monitor>
        &monitor)
    : m_window(window),
      m_autohide_controller(
          std::make_unique<
              DockAutohideController>(
              window)),
      m_preview_window(
          std::make_unique<DockPreviewWindow>()),
      m_media_playback_monitor(
          std::make_unique<
              DockMediaPlaybackMonitor>()),
      m_monitor(monitor),
      m_settings(configuration.settings),
      m_layout_request(
          configuration.layout_request)
{
    m_preview_window->set_monitor(monitor);
    m_preview_window->set_card_user_height(
        m_settings.preview_card_height());

    m_preview_window
        ->signal_pointer_entered()
        .connect(
            sigc::mem_fun(
                *this,
                &DockWindowController::
                    preview_pointer_entered));
    m_preview_window
        ->signal_pointer_left()
        .connect(
            sigc::mem_fun(
                *this,
                &DockWindowController::
                    preview_pointer_left));
    m_preview_window
        ->signal_activate_window()
        .connect(
            sigc::mem_fun(
                *this,
                &DockWindowController::
                    activate_preview_window));
    m_preview_window
        ->signal_reload_thumbnail()
        .connect(
            sigc::mem_fun(
                *this,
                &DockWindowController::
                    reload_preview_thumbnail));
    m_preview_window
        ->signal_close_window()
        .connect(
            sigc::mem_fun(
                *this,
                &DockWindowController::
                    close_preview_window));

    m_media_playback_changed =
        m_media_playback_monitor
            ->signal_changed()
            .connect(
                [this]()
                {
                    if (m_preview_desktop_id.empty())
                        return;

                    m_preview_window
                        ->set_dynamic_refresh(
                            m_media_playback_monitor
                                ->should_stream(
                                    m_preview_desktop_id),
                            m_media_playback_monitor
                                ->playing_title(
                                    m_preview_desktop_id));
                });
}

DockWindowController::~DockWindowController()
{
    hide_preview();
    cancel_show_timer();
    cancel_preview_show_timer();
    cancel_hide_timer();
    m_layout_update.disconnect();
    m_icon_geometry_update.disconnect();
    m_intellihide_update.disconnect();
    m_edge_layout_update.disconnect();
    m_icon_theme_changed.disconnect();
    m_icon_refresh.disconnect();
    m_media_playback_changed.disconnect();
    m_realize.disconnect();
    m_map.disconnect();
    m_size_allocate.disconnect();
    m_window_registry_changed.disconnect();
    m_window_registry_connection_changed.disconnect();
    m_window_geometry_changed.disconnect();
    m_dock_surface_geometry_changed.disconnect();
    m_dock_reveal_requested.disconnect();
    m_dock_add.disconnect();
    m_dock_remove.disconnect();
}

void DockWindowController::initialize()
{
    m_autohide_controller->initialize();
    m_autohide_controller->set_monitor(
        m_monitor);
    m_autohide_controller->set_mode(
        m_layout_request.autohide);

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

    m_size_allocate =
        m_window
            .signal_size_allocate()
            .connect(
                [this](
                    Gtk::Allocation &)
                {
                    schedule_icon_geometry_update();
                });

    if (m_window.m_window_registry)
    {
        m_window_registry_connection_changed =
            m_window
                .m_window_registry
                ->signal_connection_changed()
                .connect(
                    [this](bool connected)
                    {
                        if (connected)
                        {
                            m_autohide_controller
                                ->refresh_mapped_surface();
                        }
                    });

        m_window_registry_changed =
            m_window
                .m_window_registry
                ->signal_changed()
                .connect(
                    [this]()
                    {
                        cancel_show_timer();
                        m_pending_item = nullptr;
                        m_pending_tooltip_text.clear();

                        m_window
                            .synchronize_dock_items();

                        if (!m_preview_desktop_id.empty())
                        {
                            const auto items =
                                m_window.dock_items();
                            const bool preview_item_exists =
                                std::any_of(
                                    items.begin(),
                                    items.end(),
                                    [this](DockItem *item)
                                    {
                                        return item &&
                                               item->desktop_id() ==
                                                   m_preview_desktop_id;
                                    });

                            if (!preview_item_exists)
                                hide_preview();
                        }

                        std::vector<ApplicationWindowEntry>
                            all_window_entries;

                        for (auto *item :
                             m_window.dock_items())
                        {
                            item->refresh_indicator();
                            const auto entries =
                                item->window_entries();
                            all_window_entries.insert(
                                all_window_entries.end(),
                                entries.begin(),
                                entries.end());
                        }

                        m_preview_window
                            ->prime_thumbnail_cache(
                                all_window_entries);

                        schedule_icon_geometry_update();
                        schedule_intellihide_update();
                    });

        m_window_geometry_changed =
            m_window
                .m_window_registry
                ->signal_window_geometry_changed()
                .connect(
                    [this]()
                    {
                        schedule_intellihide_update();
                    });

        m_dock_surface_geometry_changed =
            m_window
                .m_window_registry
                ->signal_dock_surface_geometry_changed()
                .connect(
                    [this]()
                    {
                        schedule_icon_geometry_update();
                    });

        m_dock_reveal_requested =
            m_window
                .m_window_registry
                ->signal_dock_reveal_requested()
                .connect(
                    [this]()
                    {
                        m_autohide_controller
                            ->request_reveal();
                    });
    }

    std::vector<ApplicationWindowEntry>
        all_window_entries;

    for (auto *item : m_window.dock_items())
    {
        item->refresh_indicator();
        const auto entries = item->window_entries();
        all_window_entries.insert(
            all_window_entries.end(),
            entries.begin(),
            entries.end());
    }

    m_preview_window->prime_thumbnail_cache(
        all_window_entries);

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

    // EWMH window managers are allowed to adjust a client's initial map
    // position. Reassert the calculated dock coordinates once the X11
    // window is managed; the coalesced update is harmless on layer-shell.
    auto *display = gdk_display_get_default();
    const bool ordinary_wayland_window =
        !m_window.m_uses_layer_shell &&
        display &&
        GDK_IS_WAYLAND_DISPLAY(display);

    if (!ordinary_wayland_window)
    {
        m_map =
            m_window.signal_map().connect(
                [this]()
                {
                    schedule_layout_update();
                });
    }

    update_dock_layout();
}

void DockWindowController::apply_configuration(
    const DockConfiguration &configuration)
{
    cancel_show_timer();
    cancel_preview_show_timer();
    cancel_hide_timer();
    m_pending_item = nullptr;
    m_pending_preview_desktop_id.clear();
    m_pending_tooltip_text.clear();
    hide_tooltip();

    m_settings =
        configuration.settings;

    hide_preview();
    m_preview_window->set_card_user_height(
        m_settings.preview_card_height());

    if (m_window.m_home_item)
    {
        m_window.m_home_item->set_icon_path(
            m_settings.home_icon_path());

        if (m_settings.home_icon_enabled())
            m_window.m_home_item->show();
        else
            m_window.m_home_item->hide();
    }

    for (auto *item : m_window.dock_items())
    {
        item->set_hover_effect(
            m_settings.hover_effect());
        item->set_indicator(
            m_settings.indicator());
        item->set_indicator_color(
            m_settings.indicator_color());
        item->set_manage_all_workspaces(
            m_settings
                .manage_all_workspaces());
    }

    m_layout_request =
        configuration.layout_request;

    m_autohide_controller->set_mode(
        m_layout_request.autohide);

    schedule_intellihide_update();

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
        hide_preview();

        // Remove the reservation from the old X11 output before deriving
        // geometry for the new one. Same-monitor work-area notifications do
        // not enter this branch, so they retain the cached panel-only area
        // and cannot feed DockLight's own strut back into its position.
        m_window.prepare_x11_monitor_change();

        if (m_window.m_uses_layer_shell)
        {
            gtk_layer_set_monitor(
                GTK_WINDOW(m_window.gobj()),
                m_monitor->gobj());
        }

        m_window.m_overlay_window.set_monitor(
            m_monitor);

        m_preview_window->set_monitor(
            m_monitor);

        m_autohide_controller->set_monitor(
            m_monitor);
    }

    // The same monitor object can emit new geometry, work-area, or scale
    // values. Recalculate in both the move and geometry-change cases.
    schedule_layout_update();
}

void DockWindowController::set_preview_rounded_corners(
    bool enabled,
    int radius)
{
    m_preview_window->set_rounded_corners(
        enabled,
        radius);
}

// Reads current monitor and dock geometry, calculates placement, and then
// asks DockWindow to apply it. This is the orchestration boundary between
// live GTK state and the side-effect-free layout engine.
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

    m_output_geometry =
        output_geometry;

    auto workarea_geometry =
        m_layout_geometry.monitor_geometry(
            m_monitor);

    if (workarea_geometry.width <= 0 ||
        workarea_geometry.height <= 0)
    {
        workarea_geometry =
            output_geometry;
    }

    // Preserve the compositor's native EWMH work area for X11 placement.
    // The adjusted copy below contains Docklight's sizing-only bottom inset
    // and must not shift the actual dock surface away from its screen edge.
    const auto native_workarea_geometry =
        workarea_geometry;

    const int reported_bottom_inset =
        std::max(
            0,
            output_geometry.y +
                output_geometry.height -
                workarea_geometry.y -
                workarea_geometry.height);

    // The compositor inset keeps the dock out of an occluded screen region.
    // DOCK_MARGIN is a main-axis content margin; adding it here creates an
    // unrelated 8 px cross-axis gap when GNOME reports Docklight's own strut
    // through the monitor work area.
    const int required_bottom_inset =
        std::max(
            reported_bottom_inset,
            m_settings.minimum_bottom_workarea_inset());

    // This compatibility inset exists to keep a bottom-positioned dock out
    // of desktop environments that under-report their bottom reservation.
    // Applying it to a vertical dock shortens the main-axis work area and
    // shifts its centre upward, cancelling a real top-panel inset.
    const int missing_bottom_inset =
        m_layout_request.location ==
                DockLocation::bottom
            ? std::max(
                  0,
                  required_bottom_inset -
                      reported_bottom_inset)
            : 0;

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

    const bool edge_changed =
        m_has_applied_layout &&
        m_applied_location !=
            m_layout_request.location;

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

    m_layout_engine.apply_workarea_insets(
        placement,
        output_geometry,
        native_workarea_geometry);

    m_window.apply_dock_layout(
        placement,
        output_geometry,
        native_workarea_geometry);

    m_autohide_controller->set_placement(
        placement);

    m_placement = placement;
    m_applied_location =
        m_layout_request.location;
    m_has_applied_layout = true;

    if (m_window.m_window_registry)
    {
        const int requested_width =
            placement.width > 0
                ? placement.width
                : std::max(
                      1,
                      output_geometry.width -
                          placement.margin_left -
                          placement.margin_right);
        const int requested_height =
            placement.height > 0
                ? placement.height
                : std::max(
                      1,
                      output_geometry.height -
                          placement.margin_top -
                          placement.margin_bottom);
        const auto position =
            dock_screen_position(
                false,
                requested_width,
                requested_height);

        const WindowIconGeometry geometry{
            position.x,
            position.y,
            requested_width,
            requested_height};
        m_window.m_window_registry
            ->set_dock_placement_geometry(
                geometry);
    }

    schedule_intellihide_update();

    if (edge_changed &&
        m_window.m_uses_layer_shell)
    {
        m_edge_layout_update.disconnect();

        // The first mapped pass changes the box orientation and layer-shell
        // anchors. Recalculate once GTK has accepted the new allocation so
        // margins and the automatic exclusive zone use the new axis rather
        // than the previous horizontal/vertical size.
        m_edge_layout_update =
            Glib::signal_timeout().connect(
                [this]()
                {
                    update_dock_layout();
                    return false;
                },
                10);
    }

    schedule_icon_geometry_update();
}

void DockWindowController::inhibit_autohide()
{
    m_autohide_controller->inhibit();
}

void DockWindowController::uninhibit_autohide(
    bool pointer_inside)
{
    m_autohide_controller->uninhibit(
        pointer_inside);
}

void DockWindowController::finish_autohide_drag(
    bool pointer_inside)
{
    m_autohide_controller->finish_drag(
        pointer_inside);
}

void DockWindowController::update_effective_icon_size(
    const MonitorGeometry &monitor,
    DockOrientation orientation)
{
    auto items =
        m_window.dock_items();

    const int item_count =
        static_cast<int>(items.size()) +
        (m_window.m_home_item &&
             m_settings.home_icon_enabled()
             ? 1
             : 0);

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

    if (item_count > 0 && monitor_length > 0)
    {
        const int available_length =
            std::max(
                0,
                monitor_length -
                    2 * DockLayoutMetrics::DOCK_MARGIN);

        const int maximum_item_size =
            available_length /
            item_count;

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

    if (constrained && item_count > 0)
    {
        const int items_length =
            item_count *
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

    if (m_window.m_home_item)
    {
        m_window.m_home_item
            ->set_icon_size(
                m_window
                    .m_effective_icon_size);
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

void DockWindowController::
    schedule_icon_geometry_update()
{
    if (m_icon_geometry_update.connected())
        return;

    m_icon_geometry_update =
        Glib::signal_idle().connect(
            [this]()
            {
                update_icon_geometries();
                return false;
            });
}

void DockWindowController::
    update_icon_geometries()
{
    if (!m_window.get_realized() ||
        m_output_geometry.width <= 0 ||
        m_output_geometry.height <= 0)
    {
        return;
    }

    const auto dock_position =
        dock_screen_position(true);

    for (auto *item :
         m_window.dock_items())
    {
        const auto item_geometry =
            item->icon_geometry();

        WindowIconGeometry geometry;

        geometry.x =
            dock_position.x +
            item_geometry.x;
        geometry.y =
            dock_position.y +
            item_geometry.y;
        geometry.width =
            item_geometry.width;
        geometry.height =
            item_geometry.height;

        item->publish_icon_geometry(
            geometry);
    }
}

void DockWindowController::
    schedule_intellihide_update()
{
    if (m_intellihide_update.connected())
        return;

    m_intellihide_update =
        Glib::signal_idle().connect(
            [this]()
            {
                update_intellihide();
                return false;
            });
}

void DockWindowController::update_intellihide()
{
    bool overlap = false;

    if (m_layout_request.autohide ==
            DockAutohide::intellihide &&
        m_has_applied_layout &&
        m_window.m_window_registry &&
        m_window.m_window_registry->connected() &&
        m_window.m_window_registry
            ->capabilities()
            .provides_frame_geometry)
    {
        // A hidden or newly remapped layer-shell window can temporarily
        // expose GTK's 1x1 resize request as its allocation. Intellihide must
        // compare against the stable revealed placement, or hiding the dock
        // makes the overlap disappear and immediately reveals it again.
        const int dock_width =
            std::max(1, m_placement.width);
        const int dock_height =
            std::max(1, m_placement.height);

        const auto position =
            dock_screen_position(
                false,
                dock_width,
                dock_height);

        WindowGeometry dock_geometry;
        dock_geometry.x = position.x;
        dock_geometry.y = position.y;
        dock_geometry.width = dock_width;
        dock_geometry.height = dock_height;

        overlap =
            DockIntellihidePolicy::overlaps_dock(
                dock_geometry,
                m_window
                    .m_window_registry
                    ->windows());
    }

    m_autohide_controller
        ->set_intellihide_overlap(overlap);
}

ScreenPosition
DockWindowController::dock_screen_position(
    bool prefer_surface_geometry,
    int requested_width,
    int requested_height)
    const
{
    const int width =
        requested_width > 0
            ? requested_width
            : std::max(
                  1,
                  m_window.get_allocated_width());

    const int height =
        requested_height > 0
            ? requested_height
            : std::max(
                  1,
                  m_window.get_allocated_height());

    // X11 dock placement consumes the pre-existing EWMH work area directly,
    // so its mapped root coordinates can differ from the layer-shell-style
    // position implied by output-edge margins. Auxiliary windows must follow
    // the actual dock surface on every edge.
    auto *display = gdk_display_get_default();
    const bool wayland_display =
        display &&
        GDK_IS_WAYLAND_DISPLAY(display);

    if (!m_window.m_uses_layer_shell &&
        !wayland_display)
    {
        auto gdk_window = m_window.get_window();
        if (gdk_window)
        {
            int root_x = 0;
            int root_y = 0;
            gdk_window->get_origin(
                root_x,
                root_y);
            return {root_x, root_y};
        }
    }

    if (prefer_surface_geometry &&
        m_window.m_window_registry)
    {
        const auto surface_geometry =
            m_window
                .m_window_registry
                ->dock_surface_geometry();

        if (surface_geometry &&
            surface_geometry->width ==
                width &&
            surface_geometry->height ==
                height)
        {
            return {
                surface_geometry->x,
                surface_geometry->y};
        }
    }

    ScreenPosition position;

    if (m_placement.anchor_left)
    {
        position.x =
            m_output_geometry.x +
            m_placement.margin_left;
    }
    else if (m_placement.anchor_right)
    {
        position.x =
            m_output_geometry.x +
            m_output_geometry.width -
            m_placement.margin_right -
            width;
    }
    else
    {
        position.x =
            m_output_geometry.x +
            (m_output_geometry.width -
             width) /
                2;
    }

    if (m_placement.anchor_top)
    {
        position.y =
            m_output_geometry.y +
            m_placement.margin_top;
    }
    else if (m_placement.anchor_bottom)
    {
        position.y =
            m_output_geometry.y +
            m_output_geometry.height -
            m_placement.margin_bottom -
            height;
    }
    else
    {
        position.y =
            m_output_geometry.y +
            (m_output_geometry.height -
             height) /
                2;
    }

    return position;
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
    Gtk::Widget &item,
    const Glib::ustring &text)
{
    // Item crossing state remains authoritative while a preview layer is
    // unmapped and replaced by a tooltip. During that surface handoff GDK can
    // briefly report the physical pointer outside the dock even though it is
    // already over the newly entered item.
    m_dock_item_pointer_inside = true;

    if (!m_settings.display_tooltips())
    {
        hide_tooltip_immediately();
        return;
    }

    cancel_hide_timer();
    cancel_show_timer();
    cancel_preview_show_timer();
    m_pending_item = &item;
    m_pending_preview_desktop_id.clear();
    m_pending_tooltip_text = text;

    // Always use the show timer, including when another item's tooltip is
    // still visible during its hide grace period. This keeps show and hide
    // timing active while moving between dock items.
    m_show_timer =
        Glib::signal_timeout().connect(
            [this]()
            {
                if (m_pending_item)
                {
                    show_tooltip(
                        *m_pending_item,
                        m_pending_tooltip_text);
                }

                m_pending_item = nullptr;
                m_pending_tooltip_text.clear();
                return false;
            },
            DockConstants::TOOLTIP_SHOW_DELAY_MS);
}

void DockWindowController::schedule_show_preview(
    DockItem &item)
{
    cancel_hide_timer();
    cancel_show_timer();
    cancel_preview_show_timer();
    m_dock_item_pointer_inside = true;

    // Moving from the preview back onto the icon that owns it crosses two
    // separate layer-shell surfaces. The preview leave event starts the hide
    // grace period and the icon enter event arrives immediately afterwards.
    // Keep the already mapped preview in place instead of rebuilding and
    // showing it again after the normal preview delay; remapping the same
    // surface produces a visible double draw.
    if (!m_preview_desktop_id.empty() &&
        m_preview_desktop_id == item.desktop_id())
    {
        m_pending_item = nullptr;
        m_pending_preview_desktop_id.clear();
        m_pending_tooltip_text.clear();
        return;
    }

    m_pending_item = &item;
    m_pending_preview_desktop_id =
        item.desktop_id();
    m_pending_tooltip_text =
        item.tooltip_text();

    // A running item uses both delays: first show its label tooltip, then
    // replace that tooltip with the window preview. Keeping these timers
    // independent makes a longer preview delay useful instead of leaving the
    // hover with no feedback until the preview appears.
    if (m_settings.display_tooltips())
    {
        m_show_timer =
            Glib::signal_timeout().connect(
                [this]()
                {
                    if (m_pending_item)
                    {
                        show_tooltip(
                            *m_pending_item,
                            m_pending_tooltip_text,
                            true);
                    }

                    m_pending_item = nullptr;
                    m_pending_tooltip_text.clear();
                    return false;
                },
                DockConstants::TOOLTIP_SHOW_DELAY_MS);
    }

    m_preview_show_timer =
        Glib::signal_timeout().connect(
            [this]()
            {
                // If the configured preview delay is shorter than the
                // tooltip delay, suppress the late tooltip rather than
                // allowing it to replace an already-open preview.
                cancel_show_timer();
                m_pending_item = nullptr;
                m_pending_tooltip_text.clear();

                const auto desktop_id =
                    m_pending_preview_desktop_id;
                m_pending_preview_desktop_id.clear();

                for (auto *item : m_window.dock_items())
                {
                    if (item &&
                        item->desktop_id() == desktop_id)
                    {
                        show_preview(*item);
                        break;
                    }
                }

                return false;
            },
            m_settings.preview_show_delay());
}

void DockWindowController::schedule_hide_tooltip()
{
    m_dock_item_pointer_inside = false;
    cancel_show_timer();
    cancel_preview_show_timer();
    m_pending_item = nullptr;
    m_pending_preview_desktop_id.clear();
    m_pending_tooltip_text.clear();
    start_hide_timer();
}

void DockWindowController::hide_tooltip_immediately()
{
    cancel_show_timer();
    cancel_preview_show_timer();
    cancel_hide_timer();
    m_pending_item = nullptr;
    m_pending_preview_desktop_id.clear();
    m_pending_tooltip_text.clear();
    m_window.m_overlay_window.hide_tooltip_immediately();

    if (m_preview_window)
        m_preview_window->hide_preview_immediately();
}

void DockWindowController::dock_items_reordered()
{
    hide_tooltip_immediately();
    schedule_icon_geometry_update();
}

void DockWindowController::dock_items_changed()
{
    std::vector<ApplicationWindowEntry>
        all_window_entries;

    for (auto *item : m_window.dock_items())
    {
        if (item)
        {
            const auto entries = item->window_entries();
            all_window_entries.insert(
                all_window_entries.end(),
                entries.begin(),
                entries.end());
        }
    }

    m_preview_window->prime_thumbnail_cache(
        all_window_entries);

    // Fit the children immediately so a newly added item cannot overflow a
    // full-height dock. Defer layer-shell surface changes until the current
    // window-registry callback has returned; resizing the surface here can
    // re-enter compositor and GTK processing while the child list is changing.
    const auto orientation =
        m_layout_request.location ==
                    DockLocation::left ||
                m_layout_request.location ==
                    DockLocation::right
            ? DockOrientation::vertical
            : DockOrientation::horizontal;

    auto monitor = m_usable_monitor_geometry;

    if (monitor.width <= 0 ||
        monitor.height <= 0)
    {
        monitor =
            m_layout_geometry.output_geometry(
                m_monitor);
    }

    update_effective_icon_size(
        monitor,
        orientation);
    schedule_layout_update();
}

void DockWindowController::show_tooltip(
    Gtk::Widget &item,
    const Glib::ustring &text,
    bool preserve_pending_preview)
{
    if (!m_settings.display_tooltips())
        return;

    hide_preview(
        !preserve_pending_preview);

    const int tooltip_width =
        m_window.m_overlay_window
            .preferred_width_for(
                text);

    auto item_geometry =
        m_layout_geometry.item_geometry(
            item,
            m_window);

    auto dock_geometry =
        m_layout_geometry.dock_geometry(
            m_window);

    // KWin can publish a resized surface before its new centred position.
    // Use the synchronous layout placement for tooltips so adding or
    // removing a dock item cannot shift them to a stale surface origin.
    const auto dock_position =
        dock_screen_position(false);

    // The calculated dock position is global, while tooltip layer-shell
    // margins are relative to the selected output.
    dock_geometry.x =
        dock_position.x -
        m_output_geometry.x;
    dock_geometry.y =
        dock_position.y -
        m_output_geometry.y;
    dock_geometry.has_position = true;

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
        text,
        m_layout_request.location,
        tooltip_width,
        position);
}

void DockWindowController::hide_tooltip()
{
    m_window.m_overlay_window.hide_tooltip();
}

void DockWindowController::show_preview(
    DockItem &item,
    const WindowId &excluded_window_id)
{
    auto entries = item.window_entries();

    std::stable_sort(
        entries.begin(),
        entries.end(),
        [](const ApplicationWindowEntry &left,
           const ApplicationWindowEntry &right)
        {
            // Browser PiP and similar application-owned utility windows are
            // the media surfaces users expect to find first in the group.
            if (left.application_auxiliary !=
                right.application_auxiliary)
            {
                return left.application_auxiliary;
            }

            const auto &left_label =
                left.caption.empty()
                    ? left.id
                    : left.caption;
            const auto &right_label =
                right.caption.empty()
                    ? right.id
                    : right.caption;

            return Glib::ustring(left_label)
                       .casefold_collate_key() <
                   Glib::ustring(right_label)
                       .casefold_collate_key();
        });

    if (!excluded_window_id.empty())
    {
        entries.erase(
            std::remove_if(
                entries.begin(),
                entries.end(),
                [&excluded_window_id](
                    const ApplicationWindowEntry
                        &entry)
                {
                    return entry.id ==
                           excluded_window_id;
                }),
            entries.end());
    }

    if (entries.empty())
    {
        hide_preview();

        if (excluded_window_id.empty())
            show_tooltip(
                item,
                item.tooltip_text());

        return;
    }

    hide_tooltip();

    auto item_geometry =
        m_layout_geometry.item_geometry(
            item,
            m_window);
    auto dock_geometry =
        m_layout_geometry.dock_geometry(
            m_window);
    const auto dock_position =
        dock_screen_position(false);

    dock_geometry.x =
        dock_position.x -
        m_output_geometry.x;
    dock_geometry.y =
        dock_position.y -
        m_output_geometry.y;
    dock_geometry.has_position = true;

    auto monitor_geometry =
        m_usable_monitor_geometry;

    if (monitor_geometry.width <= 0 ||
        monitor_geometry.height <= 0)
    {
        monitor_geometry =
            m_layout_geometry.output_geometry(
                m_monitor);
        monitor_geometry.x = 0;
        monitor_geometry.y = 0;
    }

    const bool vertical_dock =
        m_layout_request.location ==
            DockLocation::left ||
        m_layout_request.location ==
            DockLocation::right;
    const int preview_distance =
        m_window.m_overlay_window
            .tooltip_distance();
    const bool dock_reserves_space =
        m_layout_request.autohide ==
            DockAutohide::none;
    const int dock_side_offset =
        vertical_dock
            ? (dock_reserves_space
                   ? 0
                   : dock_geometry.width) +
                  preview_distance
            : 0;
    const int preview_available_width =
        std::max(
            1,
            monitor_geometry.width -
                dock_side_offset -
                (vertical_dock
                     ? DockLayoutMetrics::
                           TOOLTIP_EDGE_MARGIN
                     : 2 * DockLayoutMetrics::
                           TOOLTIP_EDGE_MARGIN));

    const auto preview_size =
        m_preview_window->preferred_size(
            entries,
            preview_available_width,
            monitor_geometry.height);
    const int preview_width =
        preview_size.width;
    const int preview_height =
        preview_size.height;

    const auto position =
        m_layout_engine
            .calculate_tooltip_position(
                m_layout_request,
                monitor_geometry,
                dock_geometry,
                item_geometry,
                preview_width,
                preview_height,
                preview_distance);

    m_preview_desktop_id = item.desktop_id();

    if (!m_preview_inhibits_autohide)
    {
        m_autohide_controller->inhibit();
        m_preview_inhibits_autohide = true;
    }

    m_preview_window->show_preview(
        entries,
        m_layout_request.location,
        position,
        preview_size);
    m_preview_window->set_dynamic_refresh(
        m_media_playback_monitor->should_stream(
            m_preview_desktop_id),
        m_media_playback_monitor->playing_title(
            m_preview_desktop_id));
}

void DockWindowController::hide_preview(
    bool cancel_pending_show)
{
    if (cancel_pending_show)
    {
        cancel_preview_show_timer();
        m_pending_preview_desktop_id.clear();
    }

    m_preview_desktop_id.clear();
    m_preview_pointer_inside = false;

    if (m_preview_window)
        m_preview_window->hide_preview();

    if (m_preview_inhibits_autohide)
    {
        m_preview_inhibits_autohide = false;
        m_autohide_controller->uninhibit(
            m_dock_item_pointer_inside ||
            m_window.pointer_is_inside());
    }
}

void DockWindowController::preview_pointer_entered()
{
    m_preview_pointer_inside = true;
    cancel_hide_timer();
}

void DockWindowController::preview_pointer_left()
{
    m_preview_pointer_inside = false;
    start_hide_timer();
}

void DockWindowController::activate_preview_window(
    const WindowId &window_id)
{
    const auto desktop_id =
        m_preview_desktop_id;

    for (auto *item : m_window.dock_items())
    {
        if (item &&
            item->desktop_id() == desktop_id)
        {
            const auto entries =
                item->window_entries();
            const auto selected =
                std::find_if(
                    entries.begin(),
                    entries.end(),
                    [&window_id](
                        const ApplicationWindowEntry
                            &entry)
                    {
                        return entry.id == window_id;
                    });

            // Run the window action while GTK still exposes the button
            // event timestamp. Muffin rejects delayed X11 activation and
            // unminimize requests as untrusted focus-stealing attempts.
            if (selected != entries.end() &&
                selected->active &&
                !selected->minimized)
            {
                item->minimize_window(window_id);
            }
            else
            {
                item->show_window(window_id);
            }

            break;
        }
    }

    // Destroying preview-card widgets from their own release handler is
    // unsafe, so only the optional teardown remains deferred.
    if (m_settings.close_preview_after_activation())
    {
        Glib::signal_idle().connect_once(
            [this]()
            {
                hide_preview();
            });
    }
}

void DockWindowController::reload_preview_thumbnail(
    const WindowId &window_id)
{
    if (!m_window.m_window_registry ||
        window_id.empty())
    {
        return;
    }

    // On Xfwm, presenting an off-workspace or minimized window first visits
    // its workspace and maps it. The registry update then primes the cache
    // from the readable composite pixmap. This path is requested only by an
    // actual icon fallback in DockPreviewWindow.
    m_window.m_window_registry->present_windows(
        {window_id});
}

void DockWindowController::close_preview_window(
    const WindowId &window_id)
{
    const auto desktop_id =
        m_preview_desktop_id;

    Glib::signal_idle().connect_once(
        [this, desktop_id, window_id]()
        {
            for (auto *item : m_window.dock_items())
            {
                if (item &&
                    item->desktop_id() == desktop_id)
                {
                    if (item->close_window(window_id))
                    {
                        // Keep the mapped preview and rebuild it without the
                        // window whose close request was accepted. Hiding is
                        // reserved for the final window in the group.
                        show_preview(
                            *item,
                            window_id);
                    }
                    break;
                }
            }
        });
}

void DockWindowController::start_hide_timer()
{
    cancel_hide_timer();

    m_hide_timer =
        Glib::signal_timeout().connect(
            [this]()
            {
                if (m_dock_item_pointer_inside ||
                    m_window.pointer_is_inside() ||
                    m_preview_pointer_inside)
                {
                    return false;
                }

                hide_tooltip();
                hide_preview();
                return false;
            },
            DockConstants::TOOLTIP_HIDE_DELAY_MS);
}

void DockWindowController::cancel_show_timer()
{
    if (m_show_timer.connected())
        m_show_timer.disconnect();
}

void DockWindowController::cancel_preview_show_timer()
{
    if (m_preview_show_timer.connected())
        m_preview_show_timer.disconnect();
}

void DockWindowController::cancel_hide_timer()
{
    if (m_hide_timer.connected())
        m_hide_timer.disconnect();
}
