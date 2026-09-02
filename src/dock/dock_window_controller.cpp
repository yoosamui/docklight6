// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window_controller.cpp
//
// Implementation overview:
// Coordinates focused layout, autohide, tooltip, and preview managers with
// icon refresh and publication of compositor effect geometry.
//
// Important implementation decisions:
// - Expensive GTK reactions are coalesced through idle callbacks.
// - Pure placement is calculated before DockWindow applies side effects.
// - Effective icon size is derived from available monitor space.
// - Published icon geometry prefers compositor surface coordinates.
// - Tooltip and preview timers are owned by their focused managers.
//
// ------------------------------------------------------------

#include "dock_window_controller.h"

#include "autohide/dock_autohide_controller.h"
#include "dock_constants.h"
#include "dock_home_item.h"
#include "autohide/dock_intellihide_policy.h"
#include "dock_item.h"
#include "layout/dock_layout_metrics.h"
#include "preview_manager.h"
#include "tooltip_manager.h"
#include "layout_coordinator.h"
#include "dock_window.h"
#include "windowing/window_icon_geometry.h"
#include "windowing/window_registry.h"

#include <gtkmm/settings.h>

#include <algorithm>

namespace
{

bool same_monitor_geometry(
    const MonitorGeometry &left,
    const MonitorGeometry &right)
{
    return left.x == right.x &&
           left.y == right.y &&
           left.width == right.width &&
           left.height == right.height;
}

}

DockWindowController::DockWindowController(
    DockWindow &window,
    const DockConfiguration &configuration,
    const Glib::RefPtr<Gdk::Monitor>
        &monitor)
    : m_window(window),
      m_autohide_controller(
          std::make_unique<
              DockAutohideController>(
              window,
              configuration.settings
                  .autohide_hide_delay())),
      m_monitor(monitor),
      m_settings(configuration.settings),
      m_layout_request(
          configuration.layout_request)
{
    m_layout_coordinator =
        std::make_unique<LayoutCoordinator>(m_window);
    m_tooltip_manager = std::make_unique<TooltipManager>(
        m_window,
        [this]() { return dock_screen_position(true); });
    m_tooltip_manager->apply_configuration(m_settings);
    m_tooltip_manager->set_layout_request(m_layout_request);
    m_tooltip_manager->set_monitor(monitor);

    m_preview_manager = std::make_unique<PreviewManager>(
        m_window,
        *m_autohide_controller,
        *m_tooltip_manager,
        m_settings,
        m_layout_request,
        monitor,
        [this]() { return dock_screen_position(true); },
        [this]() { return dock_pointer_inside(); });

    apply_thumbnail_policy();

    m_tooltip_will_show =
        m_tooltip_manager->signal_will_show().connect(
            [this](bool preserve_pending_preview)
            {
                hide_preview(!preserve_pending_preview);
            });
    m_tooltip_hide_requested =
        m_tooltip_manager->signal_hide_requested().connect(
            [this]()
            {
                hide_tooltip();
                hide_preview();
            });
    m_preview_pointer_entered =
        m_preview_manager->signal_pointer_entered().connect(
            [this]() { cancel_hide_timer(); });
    m_preview_pointer_left =
        m_preview_manager->signal_pointer_left().connect(
            [this]() { start_hide_timer(); });
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
    m_realize.disconnect();
    m_map.disconnect();
    m_initial_x11_workarea_timer.disconnect();
    m_initial_x11_placement_timer.disconnect();
    m_size_allocate.disconnect();
    m_window_registry_changed.disconnect();
    m_window_registry_connection_changed.disconnect();
    m_window_geometry_changed.disconnect();
    m_dock_surface_geometry_changed.disconnect();
    m_dock_workarea_geometry_changed.disconnect();
    m_dock_reveal_requested.disconnect();
    m_dock_pointer_inside_changed.disconnect();
    m_preview_pointer_inside_changed.disconnect();
    m_preview_input_forwarding_changed.disconnect();
    m_preview_window_activated.disconnect();
    m_dock_animation_completed.disconnect();
    m_gnome_placement_fallback.disconnect();
    m_dock_add.disconnect();
    m_dock_remove.disconnect();
    m_tooltip_will_show.disconnect();
    m_tooltip_hide_requested.disconnect();
    m_preview_pointer_entered.disconnect();
    m_preview_pointer_left.disconnect();
}

void DockWindowController::initialize()
{
    const bool native_x11_window =
        m_window.surface_is_native_x11();

    if (native_x11_window)
    {
        // X11 desktops can publish provisional monitor and panel work areas
        // while the login session is still being assembled. Keep the ordinary
        // X11 toplevel invisible and avoid publishing our strut until the
        // window manager repeatedly reports the same output and work area.
        m_initial_x11_workarea_pending = true;
        m_autohide_controller
            ->begin_initial_x11_startup();
        m_initial_x11_workarea_timer =
            Glib::signal_timeout().connect(
                sigc::mem_fun(
                    *this,
                    &DockWindowController::
                        sample_initial_x11_workarea),
                DockConstants::
                    INITIAL_X11_WORKAREA_SAMPLE_INTERVAL_MS);
    }

    m_autohide_controller->set_effect(
        m_window.effective_autohide_effect());
    m_autohide_controller->initialize();
    m_autohide_controller->set_monitor(
        m_monitor);
    m_autohide_controller->set_mode(
        m_layout_request.autohide);

    if (native_x11_window)
    {
        // set_monitor() resets the X11 animation transform, including window
        // opacity. Apply the startup guard afterwards so the provisional
        // output-edge position cannot be painted before the stable work-area
        // sample places the dock beside the desktop panel.
        m_window.set_opacity(0.0);
    }

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
                            // The first allocation normally precedes the
                            // Shell extension handshake. Republish every
                            // DockItem now that the backend can accept its
                            // Mutter/KWin minimize target.
                            schedule_icon_geometry_update();
                        }

                        // Re-evaluate the configured policy on both edges of
                        // the connection. A disconnect reveals immediately;
                        // a reconnect restores normal autohide behavior.
                        m_autohide_controller->set_mode(
                            m_layout_request.autohide);
                    });

        m_window_registry_changed =
            m_window
                .m_window_registry
                ->signal_changed()
                .connect(
                    [this]()
                    {
                        // Mapping or unmapping our own X11 tooltip also
                        // changes the window registry. Do not cancel the
                        // next item's hover timer for that synthetic window
                        // event. synchronize_dock_items() already calls
                        // hide_tooltip_immediately() when it removes an item,
                        // which safely cancels a genuinely stale request.
                        m_window
                            .synchronize_dock_items();

                        if (!m_preview_manager->desktop_id().empty())
                        {
                            const auto &items =
                                m_window.dock_items();
                            const bool preview_item_exists =
                                std::any_of(
                                    items.begin(),
                                    items.end(),
                                    [this](DockItem *item)
                                    {
                                        return item &&
                                               item->desktop_id() ==
                                                   m_preview_manager
                                                       ->desktop_id();
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

                        m_preview_manager->prime_cache(
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
                        // GNOME starts this ordinary Wayland window at zero
                        // opacity until Shell confirms its first committed
                        // edge placement. Later geometry publications must
                        // not overwrite Shell's reveal/hide animation frames.
                        if (m_window.surface_initial_placement_pending())
                        {
                            m_gnome_placement_fallback.disconnect();
                            m_window.complete_surface_initial_placement();
                        }
                        m_autohide_controller->set_mode(
                            m_layout_request.autohide);
                        schedule_icon_geometry_update();
                    });

        m_dock_workarea_geometry_changed =
            m_window
                .m_window_registry
                ->signal_dock_workarea_geometry_changed()
                .connect(
                    [this]()
                    {
                        schedule_layout_update();
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

        m_dock_pointer_inside_changed =
            m_window
                .m_window_registry
                ->signal_dock_pointer_inside_changed()
                .connect(
                    [this](bool inside)
                    {
                        m_backend_dock_pointer_state_known = true;
                        m_backend_dock_pointer_inside = inside;
                        m_autohide_controller
                            ->set_backend_pointer_inside(inside);
                        if (inside)
                            cancel_hide_timer();
                        else
                            start_hide_timer();
                    });

        m_preview_pointer_inside_changed =
            m_window
                .m_window_registry
                ->signal_preview_pointer_inside_changed()
                .connect(
                    [this](bool inside)
                    {
                        shell_preview_pointer_changed(
                            inside);
                    });

        m_preview_input_forwarding_changed =
            m_window
                .m_window_registry
                ->signal_preview_input_forwarding_changed()
                .connect(
                    [this](bool forwarding)
                    {
                        m_preview_manager->set_input_forwarding(
                            forwarding);
                    });

        m_preview_window_activated =
            m_window
                .m_window_registry
                ->signal_preview_window_activated()
                .connect(
                    sigc::mem_fun(
                        *this,
                        &DockWindowController::
                            activate_preview_window));

        m_dock_animation_completed =
            m_window
                .m_window_registry
                ->signal_dock_animation_completed()
                .connect(
                    [this](bool hidden)
                    {
                        m_autohide_controller
                            ->finish_shell_animation(hidden);
                    });
    }

    if (m_window.surface_initial_placement_pending())
    {
        m_gnome_placement_fallback =
            Glib::signal_timeout().connect(
                [this]()
                {
                    if (!m_window.surface_initial_placement_pending())
                        return false;

                    m_window.complete_surface_initial_placement();
                    m_autohide_controller->set_mode(
                        m_layout_request.autohide);
                    g_warning(
                        "GNOME Shell did not place the Docklight surface; "
                        "keeping the dock visible until window integration "
                        "is available");
                    return false;
                },
                DockConstants::
                    GNOME_PLACEMENT_FALLBACK_DELAY_MS);
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

    m_preview_manager->prime_cache(all_window_entries);

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
    const bool ordinary_wayland_window =
        m_window.surface_is_ordinary_wayland();

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

bool DockWindowController::sample_initial_x11_workarea()
{
    const auto output_geometry =
        m_window.surface_output_geometry();
    const auto workarea_geometry =
        m_window.surface_work_area();

    ++m_initial_x11_workarea_sample_attempt_count;

    const bool valid_sample =
        output_geometry.width > 0 &&
        output_geometry.height > 0 &&
        workarea_geometry.width > 0 &&
        workarea_geometry.height > 0;

    if (valid_sample &&
        m_initial_x11_workarea_stable_sample_count > 0 &&
        same_monitor_geometry(
            m_initial_x11_workarea_sample,
            workarea_geometry) &&
        same_monitor_geometry(
            m_initial_x11_output_sample,
            output_geometry))
    {
        ++m_initial_x11_workarea_stable_sample_count;
    }
    else if (valid_sample)
    {
        m_initial_x11_workarea_sample =
            workarea_geometry;
        m_initial_x11_output_sample =
            output_geometry;
        m_initial_x11_workarea_stable_sample_count = 1;
    }
    else
    {
        m_initial_x11_workarea_stable_sample_count = 0;
    }

    const bool stable =
        m_initial_x11_workarea_stable_sample_count >=
            DockConstants::
                INITIAL_X11_WORKAREA_REQUIRED_STABLE_SAMPLES;
    const bool exhausted =
        m_initial_x11_workarea_sample_attempt_count >=
            DockConstants::
                INITIAL_X11_WORKAREA_MAX_SAMPLE_ATTEMPTS;

    if (!stable && !exhausted)
        return true;

    m_initial_x11_workarea_pending = false;
    update_dock_layout();

    // apply_dock_layout() submits an asynchronous X11 move and autohide's
    // placement reset restores opacity. Reapply the guard before returning to
    // the main loop, then choose the initial visibility state only after the
    // window manager reports the requested root coordinates. This prevents
    // one painted frame at the provisional edge.
    m_window.set_opacity(0.0);

    const int requested_width =
        m_placement.width > 0
            ? m_placement.width
            : std::max(
                  1,
                  m_output_geometry.width -
                      m_placement.margin_left -
                      m_placement.margin_right);
    const int requested_height =
        m_placement.height > 0
            ? m_placement.height
            : std::max(
                  1,
                  m_output_geometry.height -
                      m_placement.margin_top -
                      m_placement.margin_bottom);
    m_initial_x11_target_position =
        calculated_dock_screen_position(
            requested_width,
            requested_height);
    m_initial_x11_placement_attempt_count = 0;
    m_initial_x11_placement_timer.disconnect();
    m_initial_x11_placement_timer =
        Glib::signal_timeout().connect(
            sigc::mem_fun(
                *this,
                &DockWindowController::
                    finish_initial_x11_placement),
            DockConstants::
                INITIAL_X11_PLACEMENT_POLL_INTERVAL_MS);
    return false;
}

bool DockWindowController::finish_initial_x11_placement()
{
    int x = 0;
    int y = 0;
    m_window.get_position(x, y);

    if (x == m_initial_x11_target_position.x &&
        y == m_initial_x11_target_position.y)
    {
        m_autohide_controller
            ->complete_initial_x11_startup();
        return false;
    }

    ++m_initial_x11_placement_attempt_count;
    if (m_initial_x11_placement_attempt_count <
        DockConstants::
            INITIAL_X11_PLACEMENT_MAX_ATTEMPTS)
    {
        return true;
    }

    // Do not leave the dock permanently transparent if a non-conforming
    // window manager adjusts the requested position. The final coordinates
    // are still preferable to painting its provisional startup position.
    m_autohide_controller
        ->complete_initial_x11_startup();
    g_warning(
        "X11 did not confirm Docklight's initial position "
        "(%d,%d); using compositor position (%d,%d)",
        m_initial_x11_target_position.x,
        m_initial_x11_target_position.y,
        x,
        y);
    return false;
}

void DockWindowController::apply_configuration(
    const DockConfiguration &configuration)
{
    cancel_show_timer();
    cancel_preview_show_timer();
    cancel_hide_timer();
    hide_tooltip();

    const bool location_changed =
        m_layout_request.location !=
        configuration.layout_request.location;

    if (location_changed)
    {
        // The existing X11 base area may describe a panel which occupied the
        // previous edge. Re-sample dock clients before placing the new edge.
        m_window.prepare_surface_change();
    }

    m_settings =
        configuration.settings;

    hide_preview();
    m_tooltip_manager->apply_configuration(m_settings);
    m_preview_manager->apply_configuration(m_settings);
    apply_thumbnail_policy();

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
    m_tooltip_manager->set_layout_request(m_layout_request);
    m_preview_manager->set_layout_request(m_layout_request);

    m_autohide_controller->set_hide_delay(
        m_settings.autohide_hide_delay());
    m_autohide_controller->set_effect(
        m_window.effective_autohide_effect());
    m_autohide_controller->set_mode(
        m_layout_request.autohide);

    schedule_intellihide_update();

    // Icon size, orientation, alignment, reservation, and visual settings all
    // converge in update_dock_layout(). Coalesce rapid configuration saves
    // into one recalculation on the GTK main loop.
    schedule_layout_update();
}

void DockWindowController::apply_thumbnail_policy()
{
    auto policy =
        WindowThumbnailPolicy::capture_on_demand;

    if (m_settings.display_preview() &&
        m_window.m_window_registry)
    {
        policy = m_window.m_window_registry
                     ->capabilities()
                     .thumbnail_policy;
    }

    m_preview_manager->set_thumbnail_policy(policy);
}

void DockWindowController::set_monitor(
    const Glib::RefPtr<Gdk::Monitor>
        &monitor)
{
    if (!monitor)
        return;

    const bool monitor_changed =
        monitor != m_monitor;
    const auto next_output_geometry =
        monitor_changed
            ? m_layout_geometry.output_geometry(
                  monitor)
            : m_window.surface_output_geometry();
    const bool monitor_geometry_changed =
        m_output_geometry.width > 0 &&
        m_output_geometry.height > 0 &&
        (next_output_geometry.x !=
             m_output_geometry.x ||
         next_output_geometry.y !=
             m_output_geometry.y ||
         next_output_geometry.width !=
             m_output_geometry.width ||
         next_output_geometry.height !=
             m_output_geometry.height);
    const bool output_changed =
        monitor_changed ||
        monitor_geometry_changed;

    m_monitor = monitor;

    if (output_changed)
    {
        hide_tooltip();
        hide_preview();

        // Remove the reservation before deriving geometry for a different
        // output or a moved/resized selected output. Work-area-only signals
        // do not enter this branch, so DockLight's own strut cannot feed back
        // into its position.
        m_window.prepare_surface_change();

        if (monitor_changed)
            m_window.set_surface_monitor(
                m_monitor);

        m_tooltip_manager->set_monitor(m_monitor);
        m_preview_manager->set_monitor(m_monitor);

        m_autohide_controller->set_monitor(
            m_monitor);
    }

    // The same monitor object can emit new geometry, work-area, or scale
    // values. Recalculate in both the move and geometry-change cases.
    schedule_layout_update();
}

void DockWindowController::request_reveal()
{
    m_autohide_controller->request_reveal();
}

void DockWindowController::set_preview_rounded_corners(
    bool enabled,
    int radius)
{
    m_preview_manager->set_rounded_corners(
        enabled,
        radius);
}

// Reads current monitor and dock geometry, calculates placement, and then
// asks DockWindow to apply it. This is the orchestration boundary between
// live GTK state and the side-effect-free layout engine.
void DockWindowController::update_dock_layout()
{
    if (m_initial_x11_workarea_pending)
        return;

    const auto monitor_layout =
        m_layout_coordinator->resolve_monitor_layout(
            m_settings,
            m_layout_request);
    if (!monitor_layout.valid())
    {
        // Never submit the unconstrained natural size to layer-shell. The
        // realize callback will retry once GDK can identify the output.
        return;
    }

    const auto output_geometry = monitor_layout.output;
    const auto native_workarea_geometry =
        monitor_layout.native_workarea;
    const auto workarea_geometry =
        monitor_layout.sizing_workarea;
    m_output_geometry = output_geometry;
    m_usable_monitor_geometry =
        monitor_layout.usable_monitor;

    // Tooltip and preview layout remains output-relative. Native layer-shell
    // overlays are arranged inside this usable area, so their surface classes
    // need its origin and size to translate margins exactly once.
    m_tooltip_manager->set_layout_geometry(
        m_usable_monitor_geometry,
        m_output_geometry);
    m_preview_manager->set_layout_geometry(
        m_usable_monitor_geometry,
        m_output_geometry);

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

    // Layer-shell positions anchored surfaces from the usable edge left by
    // earlier exclusive zones. Adding the same compositor work-area inset as
    // a margin would count a Plasma panel twice. X11 and ordinary Wayland
    // windows still need explicit output-relative margins.
    if (!m_window.surface_uses_native_placement())
    {
        m_layout_engine.apply_workarea_insets(
            placement,
            output_geometry,
            native_workarea_geometry);
    }

    m_window.apply_dock_layout(
        placement,
        output_geometry,
        native_workarea_geometry);

    m_placement = placement;

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
    // X11 moves are asynchronous. Keep the synchronous target shared by the
    // hidden transform and the window-system backend so neither observes the
    // previous dock size's root coordinates.
    const auto position =
        calculated_dock_screen_position(
            requested_width,
            requested_height);

    m_autohide_controller->set_placement(
        placement,
        position);

    m_applied_location =
        m_layout_request.location;
    m_has_applied_layout = true;

    if (m_window.m_window_registry)
    {
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
        m_window.surface_uses_native_placement())
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
                DockConstants::
                    EDGE_LAYOUT_SETTLE_DELAY_MS);
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
    const auto &items =
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
    if (m_window.surface_is_native_x11())
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

    return calculated_dock_screen_position(
        width,
        height);
}

ScreenPosition
DockWindowController::
    calculated_dock_screen_position(
        int width,
        int height) const
{
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
    const auto &items =
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
    m_preview_manager->cancel_show_timer();
    m_tooltip_manager->schedule_show(item, text);
}

void DockWindowController::schedule_show_preview(DockItem &item)
{
    m_preview_manager->schedule_show(
        item,
        m_settings.preview_show_delay());
}

void DockWindowController::schedule_hide_tooltip(Gtk::Widget &item)
{
    const bool was_hovered =
        m_tooltip_manager->hovered_item() == &item;
    m_tooltip_manager->schedule_hide(item);
    if (!was_hovered)
        return;

    m_preview_manager->cancel_show_timer();
    start_hide_timer();
}

void DockWindowController::hide_tooltip_immediately()
{
    m_tooltip_manager->hide_immediately();
    m_preview_manager->hide_immediately();
}

void DockWindowController::dock_items_reordered()
{
    hide_tooltip_immediately();
    schedule_icon_geometry_update();
}

void DockWindowController::dock_items_changed()
{
    std::vector<ApplicationWindowEntry> all_window_entries;
    for (auto *item : m_window.dock_items())
    {
        if (!item)
            continue;
        const auto entries = item->window_entries();
        all_window_entries.insert(
            all_window_entries.end(),
            entries.begin(),
            entries.end());
    }
    m_preview_manager->prime_cache(all_window_entries);

    const auto orientation =
        m_layout_request.location == DockLocation::left ||
                m_layout_request.location == DockLocation::right
            ? DockOrientation::vertical
            : DockOrientation::horizontal;
    auto monitor = m_usable_monitor_geometry;
    if (monitor.width <= 0 || monitor.height <= 0)
        monitor = m_layout_geometry.output_geometry(m_monitor);

    update_effective_icon_size(monitor, orientation);
    schedule_layout_update();
}

void DockWindowController::hide_tooltip()
{
    m_tooltip_manager->hide();
}

void DockWindowController::hide_preview(bool cancel_pending_show)
{
    m_preview_manager->hide(cancel_pending_show);
}

void DockWindowController::shell_preview_pointer_changed(bool inside)
{
    m_preview_manager->set_shell_pointer_inside(inside);
}

void DockWindowController::activate_preview_window(
    const WindowId &window_id)
{
    m_preview_manager->activate_window(window_id);
}

bool DockWindowController::dock_pointer_inside() const
{
    return m_backend_dock_pointer_state_known
        ? m_backend_dock_pointer_inside
        : m_window.pointer_is_inside();
}

void DockWindowController::start_hide_timer()
{
    m_tooltip_manager->start_hide_timer(
        [this]()
        {
            return m_tooltip_manager->pointer_inside() ||
                   dock_pointer_inside() ||
                   m_preview_manager->pointer_inside();
        });
}

void DockWindowController::cancel_show_timer()
{
    m_tooltip_manager->cancel_show_timer();
}

void DockWindowController::cancel_preview_show_timer()
{
    m_preview_manager->cancel_show_timer();
}

void DockWindowController::cancel_hide_timer()
{
    m_tooltip_manager->cancel_hide_timer();
}

bool DockWindowController::preview_input_forwarding() const
{
    return m_preview_manager->input_forwarding();
}
