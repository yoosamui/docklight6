// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_preview_window.cpp
//
// Implementation overview:
// Implements DockPreviewWindow lifetime, configuration, input events, and
// public signals.
//
// Important implementation decisions:
// - Cohesive cache, animation, and layout methods live in neighboring build
//   units while sharing the same class declaration.
//
// ------------------------------------------------------------

#include "dock_preview_window.h"
#include "dock_preview_window_internal.h"
#include "presentation/docklight_surface_identity.h"

#include <gtk-layer-shell.h>

#include <algorithm>

namespace
{

bool uses_muffin_session()
{
    const char *desktop =
        g_getenv("XDG_CURRENT_DESKTOP");
    if (!desktop)
        desktop = g_getenv("XDG_SESSION_DESKTOP");
    if (!desktop)
        return false;

    auto *normalized = g_ascii_strdown(desktop, -1);
    const bool result =
        normalized &&
        std::string(normalized).find("cinnamon") !=
            std::string::npos;
    g_free(normalized);
    return result;
}

} // namespace

DockPreviewWindow::DockPreviewWindow()
{
    m_preview_color.set("#69aaff");

    set_decorated(false);
    set_resizable(false);
    set_app_paintable(true);
    set_accept_focus(false);
    set_focus_on_map(false);
    gtk_window_set_role(
        GTK_WINDOW(gobj()),
        DocklightSurfaceIdentity::PREVIEW_ROLE);

    // CSS rounds the child surface, not the native X11 toplevel. Ensure the
    // pixels outside that surface can actually be transparent on X11 by
    // selecting an RGBA visual before realization and clearing the complete
    // toplevel on every draw.
    auto screen = get_screen();

    if (screen)
    {
        auto rgba_visual = screen->get_rgba_visual();

        if (rgba_visual)
        {
            gtk_widget_set_visual(
                GTK_WIDGET(gobj()),
                rgba_visual->gobj());
        }
    }

    signal_draw().connect(
        [](const Cairo::RefPtr<Cairo::Context> &context)
        {
            context->save();
            context->set_operator(Cairo::OPERATOR_SOURCE);
            context->set_source_rgba(0.0, 0.0, 0.0, 0.0);
            context->paint();
            context->restore();
            return false;
        },
        false);

    add_events(
        Gdk::ENTER_NOTIFY_MASK |
        Gdk::POINTER_MOTION_MASK |
        Gdk::LEAVE_NOTIFY_MASK);

    m_row.set_margin_start(WINDOW_PADDING);
    m_row.set_margin_end(WINDOW_PADDING);
    m_row.set_margin_top(WINDOW_PADDING);
    m_row.set_margin_bottom(WINDOW_PADDING);
    m_row.set_halign(Gtk::ALIGN_START);
    m_row.set_valign(Gtk::ALIGN_START);

    m_scroller.set_policy(
        Gtk::POLICY_NEVER,
        Gtk::POLICY_NEVER);
    m_scroller.add(m_row);
    m_surface.add(m_scroller);
    add(m_surface);

    get_style_context()->add_class(
        "dock-preview-window");
    m_surface.get_style_context()->add_class(
        "dock-preview");

    m_css = Gtk::CssProvider::create();
    get_style_context()->add_provider(
        m_css,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    m_surface.get_style_context()->add_provider(
        m_css,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    m_corner_css = Gtk::CssProvider::create();
    m_surface.get_style_context()->add_provider(
        m_corner_css,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    m_css->load_from_data(
        "window.dock-preview-window {"
        " background-color: transparent;"
        "}"
        ".dock-preview {"
        " background-color: rgba(28, 28, 32, 0.96);"
        " border: 1px solid rgba(255,255,255,0.28);"
        "}"
        ".dock-preview-card {"
        " background: transparent;"
        " border: 1px solid rgba(255,255,255,0.25);"
        " border-radius: 7px;"
        "}"
        ".dock-preview-header {"
        " border-bottom: 1px solid rgba(255,255,255,0.18);"
        "}"
        ".dock-preview-title {"
        " color: white; font-size: 11px;"
        "}"
        ".dock-preview-close {"
        " min-width: 0; min-height: 0;"
        " padding: 0; border: 0;"
        " background: transparent; color: white;"
        "}"
        ".dock-preview-close:hover {"
        " background: rgba(220,60,60,0.9);"
        " border-radius: 11px;"
        "}");

    set_rounded_corners(true, 10);

    auto *window = GTK_WINDOW(gobj());
    m_uses_layer_shell = gtk_layer_is_supported();

    if (m_uses_layer_shell)
    {
        gtk_layer_init_for_window(window);
        gtk_layer_set_namespace(
            window,
            DocklightSurfaceIdentity::
                PREVIEW_NAMESPACE);
        gtk_layer_set_layer(
            window,
            GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_keyboard_mode(
            window,
            GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        gtk_layer_set_exclusive_zone(window, 0);
    }
    else
    {
        set_type_hint(Gdk::WINDOW_TYPE_HINT_UTILITY);
        set_skip_taskbar_hint(true);
        set_skip_pager_hint(true);
        set_keep_above(true);
        stick();
        set_position(Gtk::WIN_POS_NONE);
    }

    signal_map().connect(
        [this]()
        {
            if (m_has_position)
            {
                const auto allocation =
                    get_allocation();
                apply_allocated_position(
                    std::max(
                        1,
                        allocation.get_width()),
                    std::max(
                        1,
                        allocation.get_height()));
            }
        });

    signal_size_allocate().connect(
        [this](Gtk::Allocation &allocation)
        {
            if (!m_has_position || !get_visible())
                return;

            apply_allocated_position(
                std::max(
                    1,
                    allocation.get_width()),
                std::max(
                    1,
                    allocation.get_height()));

            complete_presentation();
        });
}

DockPreviewWindow::~DockPreviewWindow()
{
    cancel_opacity_animation();
    m_gnome_preview_remap_delay.disconnect();
    m_gnome_preview_reveal_delay.disconnect();
    m_thumbnail_cache_refresh.disconnect();
    m_thumbnail_recovery_delay.disconnect();
    for (auto &retry : m_thumbnail_cache_retries)
        retry.second.disconnect();
    m_thumbnail_cache_retries.clear();
    for (auto &delay : m_thumbnail_cache_settle_delays)
        delay.second.disconnect();
    m_thumbnail_cache_settle_delays.clear();
    m_replacing_gnome_wayland_preview = false;
    stop_live_streams();
    ++m_generation;

    // Keep the most recent active frames for the next DockLight start. Use a
    // copy because a successful write removes the id from the dirty set.
    const auto dirty_thumbnails =
        m_thumbnail_cache_dirty;
    for (const auto &window_id : dirty_thumbnails)
    {
        const auto cached =
            m_thumbnail_cache.find(window_id);
        if (cached != m_thumbnail_cache.end())
        {
            persist_thumbnail_cache(
                window_id,
                cached->second);
        }
    }

    clear_cards();
}

void DockPreviewWindow::set_monitor(
    const Glib::RefPtr<Gdk::Monitor> &monitor)
{
    if (monitor)
    {
        Gdk::Rectangle geometry;
        monitor->get_geometry(geometry);
        m_monitor_geometry = {
            geometry.get_x(),
            geometry.get_y(),
            geometry.get_width(),
            geometry.get_height()};
        m_workarea_geometry = {
            0,
            0,
            geometry.get_width(),
            geometry.get_height()};
    }

    if (m_uses_layer_shell)
    {
        gtk_layer_set_monitor(
            GTK_WINDOW(gobj()),
            monitor ? monitor->gobj() : nullptr);
    }
}

void DockPreviewWindow::set_workarea_geometry(
    const MonitorGeometry &geometry)
{
    if (geometry.width > 0 &&
        geometry.height > 0)
    {
        m_workarea_geometry = geometry;
    }
}

void DockPreviewWindow::set_card_user_height(
    int height)
{
    m_card_user_height =
        height == CARD_USER_HEIGHT ||
                (height >= MIN_HEIGHT &&
                 height <= MAX_HEIGHT)
            ? height
            : CARD_USER_HEIGHT;
}

void DockPreviewWindow::set_preview_color(
    const std::string &color)
{
    Gdk::RGBA parsed;
    if (!parsed.set(color))
        parsed.set("#69aaff");

    m_preview_color = parsed;

    for (auto &entry : m_thumbnail_targets)
    {
        auto &target = entry.second;
        if (target.image)
            target.image->set_preview_color(
                m_preview_color);
    }

    if (m_thumbnail_provider
            .supports_gnome_live_previews())
    {
        m_thumbnail_provider.set_gnome_preview_color(
            m_preview_color.get_red(),
            m_preview_color.get_green(),
            m_preview_color.get_blue(),
            m_preview_color.get_alpha());
    }
}

void DockPreviewWindow::set_rounded_corners(
    bool enabled,
    int radius)
{
    auto context =
        m_surface.get_style_context();

    if (enabled)
        context->add_class("dock-rounded");
    else
        context->remove_class("dock-rounded");

    const int effective_radius =
        enabled
            ? std::min(
                  std::max(0, radius),
                  MIN_HEIGHT / 2)
            : 0;

    m_corner_css->load_from_data(
        ".dock-preview { border-radius: " +
        std::to_string(effective_radius) +
        "px; }");
}

void DockPreviewWindow::set_thumbnail_policy(
    WindowThumbnailPolicy policy)
{
    m_thumbnail_policy = policy;
    m_thumbnail_provider.set_x11_window_redirection(
        uses_redirected_thumbnail_capture());
}

void DockPreviewWindow::set_dynamic_refresh(
    bool enabled,
    const std::string &media_title)
{
    // Firefox can leave its single browser-wide MPRIS player associated with
    // a different tab after a video enters Picture-in-Picture. A recognized
    // X11 application auxiliary is itself sufficient evidence that the
    // preview needs live frames; do not let stale MPRIS metadata freeze it.
    const bool has_x11_application_auxiliary =
        !m_uses_layer_shell &&
        std::any_of(
            m_thumbnail_targets.begin(),
            m_thumbnail_targets.end(),
            [](const auto &entry)
            {
                return !entry.second.minimized &&
                       entry.second.on_current_desktop &&
                       entry.second.application_auxiliary;
            });

    // Browser MPRIS state is only a hint: it can be absent, delayed, or tied
    // to another tab. Muffin keeps mapped current-workspace pixmaps live, so
    // keep the refresh scheduler active for those windows and capture them
    // directly below. Hidden compositor pixmaps are allowed to remain frozen.
    const bool has_visible_current_muffin_target =
        !m_uses_layer_shell &&
        uses_muffin_session() &&
        !m_thumbnail_provider.supports_gnome_live_previews() &&
        std::any_of(
            m_thumbnail_targets.begin(),
            m_thumbnail_targets.end(),
            [](const auto &entry)
            {
                return !entry.second.minimized &&
                       entry.second.on_current_desktop;
            });

    m_dynamic_refresh =
        enabled ||
        has_x11_application_auxiliary ||
        has_visible_current_muffin_target;

    if (!media_title.empty())
        m_media_title = media_title;

    if (!enabled)
        m_media_title.clear();

    if (m_thumbnail_provider.supports_gnome_live_previews() &&
        get_visible() &&
        !m_thumbnail_targets.empty())
    {
        start_live_streams();
    }
    else if (m_dynamic_refresh &&
        get_visible() &&
        !m_thumbnail_targets.empty())
    {
        start_live_streams();
    }
    else
    {
        stop_live_streams();
    }
}

void DockPreviewWindow::set_input_forwarding(
    bool forwarding)
{
    m_input_forwarding = forwarding;
}

bool DockPreviewWindow::visible_for(
    const WindowId &window_id) const
{
    return get_visible() &&
           std::find(
               m_window_ids.begin(),
               m_window_ids.end(),
               window_id) != m_window_ids.end();
}

bool DockPreviewWindow::on_enter_notify_event(
    GdkEventCrossing *event)
{
    if (m_input_forwarding)
        return false;

    if (!event ||
        event->detail != GDK_NOTIFY_INFERIOR)
    {
        m_close_pointer_origin_valid = false;
        m_pointer_entered.emit();
    }

    return Gtk::Window::on_enter_notify_event(event);
}

bool DockPreviewWindow::on_motion_notify_event(
    GdkEventMotion *event)
{
    if (!m_input_forwarding &&
        (!m_close_pointer_origin_valid ||
         std::abs(event->x_root - m_close_pointer_root_x) > 0.5 ||
         std::abs(event->y_root - m_close_pointer_root_y) > 0.5))
    {
        m_close_pointer_origin_valid = false;
        m_pointer_moved.emit();
    }

    return Gtk::Window::on_motion_notify_event(event);
}

bool DockPreviewWindow::on_leave_notify_event(
    GdkEventCrossing *event)
{
    if (m_input_forwarding)
        return false;

    if (!event ||
        event->detail != GDK_NOTIFY_INFERIOR)
    {
        m_pointer_left.emit();
    }

    return Gtk::Window::on_leave_notify_event(event);
}

sigc::signal<void> &
DockPreviewWindow::signal_pointer_entered()
{
    return m_pointer_entered;
}

sigc::signal<void> &
DockPreviewWindow::signal_pointer_moved()
{
    return m_pointer_moved;
}

sigc::signal<void> &
DockPreviewWindow::signal_pointer_left()
{
    return m_pointer_left;
}

sigc::signal<void, const WindowId &> &
DockPreviewWindow::signal_activate_window()
{
    return m_activate_window;
}

sigc::signal<void, const WindowId &> &
DockPreviewWindow::signal_reload_thumbnail()
{
    return m_reload_thumbnail;
}

sigc::signal<void, const WindowId &, bool> &
DockPreviewWindow::signal_close_window()
{
    return m_close_window;
}
