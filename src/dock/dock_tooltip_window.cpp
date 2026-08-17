// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_tooltip_window.cpp
//
// Implementation overview:
// Implements tooltip measurement, styling, delayed remapping, and
// application of precomputed layer-shell margins.
//
// Important implementation decisions:
// - Text is measured before the layout engine chooses a position.
// - Repeated identical requests do not restart visible tooltip state.
// - Input is transparent so the overlay never interrupts dock hovering.
// - Remapping is delayed briefly to replay compositor show animation.
//
// ------------------------------------------------------------

#include "dock_tooltip_window.h"

#include "dock_constants.h"
#include "layout/dock_layout_metrics.h"
#include "presentation/docklight_surface_identity.h"

#include <gtk-layer-shell.h>

#include <algorithm>
#include <cmath>
#include <glibmm/main.h>
#include <pangomm/layout.h>
#include <string>

namespace
{

double ease_opacity(
    double progress,
    bool /* hiding */)
{
    progress = std::clamp(
        progress,
        0.0,
        1.0);

    // Match the preview's fade-only transition: smoothstep gives appearing
    // and disappearing tooltips the same gentle start and finish.
    return progress * progress *
           (3.0 - 2.0 * progress);
}

ScreenPosition animation_offset(
    DockLocation location)
{
    constexpr int distance = 3;

    switch (location)
    {
    case DockLocation::top:
        return {0, -distance};
    case DockLocation::bottom:
        return {0, distance};
    case DockLocation::left:
        return {-distance, 0};
    case DockLocation::right:
        return {distance, 0};
    }

    return {};
}

}

DockTooltipWindow::DockTooltipWindow()
{
    set_decorated(false);
    set_resizable(false);
    set_app_paintable(true);
    set_accept_focus(false);
    gtk_window_set_role(
        GTK_WINDOW(gobj()),
        DocklightSurfaceIdentity::TOOLTIP_ROLE);

    // X11 otherwise commonly realizes this undecorated toplevel with an
    // opaque visual. In that case GTK clips the tooltip background to its
    // CSS radius, but the transparent pixels around it are displayed as the
    // square window background. Request the alpha-capable visual before the
    // native window is realized.
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

    // The width is set for each label. The height stays stable so tooltip
    // placement remains predictable on every dock edge.
    set_size_request(
        -1,
        DockLayoutMetrics::TOOLTIP_HEIGHT);

    m_label.set_halign(Gtk::ALIGN_CENTER);
    m_label.set_valign(Gtk::ALIGN_CENTER);

    // These margins are part of the label's preferred size, so variable-width
    // tooltips keep readable padding without a hard-coded window width.
    m_label.set_margin_start(12);
    m_label.set_margin_end(12);

    m_event_box.add(m_label);
    add(m_event_box);

    m_event_box.get_style_context()->add_class("dock-tooltip");
    get_style_context()->add_class("dock-tooltip-window");

    m_visual_css = Gtk::CssProvider::create();
    m_event_box.get_style_context()->add_provider(
        m_visual_css,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    auto window = GTK_WINDOW(gobj());

    m_uses_layer_shell = gtk_layer_is_supported();

    if (m_uses_layer_shell)
    {
        gtk_layer_init_for_window(window);
        gtk_layer_set_keyboard_interactivity(window, FALSE);
        gtk_layer_set_namespace(
            window,
            DocklightSurfaceIdentity::
                TOOLTIP_NAMESPACE);
        gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_exclusive_zone(window, 0);
    }
    else
    {
        set_type_hint(Gdk::WINDOW_TYPE_HINT_TOOLTIP);
        set_skip_taskbar_hint(true);
        set_skip_pager_hint(true);
        set_keep_above(true);
        set_position(Gtk::WIN_POS_NONE);
    }

    signal_realize().connect(
        sigc::mem_fun(
            *this,
            &DockTooltipWindow::make_input_transparent));

    signal_map().connect(
        [this]()
        {
            make_input_transparent();
        });
}

void DockTooltipWindow::set_monitor(
    const Glib::RefPtr<Gdk::Monitor>
        &monitor)
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
            monitor
                ? monitor->gobj()
                : nullptr);
    }
}

void DockTooltipWindow::set_workarea_geometry(
    const MonitorGeometry &geometry)
{
    if (geometry.width > 0 &&
        geometry.height > 0)
    {
        m_workarea_geometry = geometry;
    }
}

void DockTooltipWindow::set_rounded_corners(
    bool enabled,
    int radius,
    int icon_size)
{
    m_icon_size = std::max(1, icon_size);

    auto context = m_event_box.get_style_context();

    if (enabled)
        context->add_class("dock-rounded");
    else
        context->remove_class("dock-rounded");

    const int effective_radius =
        enabled
            ? std::min(
                  std::max(0, radius),
                  DockLayoutMetrics::TOOLTIP_HEIGHT / 2)
            : 0;

    m_tooltip_height =
        DockLayoutMetrics::tooltip_height_for(
            m_icon_size);

    m_tooltip_distance =
        DockLayoutMetrics::tooltip_distance_for(
            m_icon_size);

    const int label_padding =
        DockLayoutMetrics::tooltip_label_padding_for(
            m_icon_size);

    m_label.set_margin_start(label_padding);
    m_label.set_margin_end(label_padding);

    set_size_request(-1, m_tooltip_height);

    m_visual_css->load_from_data(
        ".dock-tooltip { border-radius: " +
        std::to_string(effective_radius) +
        "px; }"
        ".dock-tooltip label { font-size: " +
        std::to_string(
            DockLayoutMetrics::tooltip_font_size_for(
                m_icon_size)) +
        "px; }");
}

int DockTooltipWindow::tooltip_height() const
{
    return m_tooltip_height;
}

int DockTooltipWindow::tooltip_distance() const
{
    return m_tooltip_distance;
}

int DockTooltipWindow::preferred_width_for(
    const Glib::ustring &text)
{
    m_label.set_text(text);

    // get_preferred_width() is not reliable before this independent toplevel
    // has been mapped: it can return the minimum 80px width on first hover.
    // Pango's label layout is available immediately after setting the text.
    int text_width = 0;
    int text_height = 0;

    m_label.get_layout()->get_pixel_size(
        text_width,
        text_height);

    const auto border =
        m_event_box.get_style_context()->get_border();

    const int natural_width =
        text_width +
        m_label.get_margin_start() +
        m_label.get_margin_end() +
        border.get_left() +
        border.get_right();

    // Use the natural label width so long application names are not clipped.
    return std::max(
        DockLayoutMetrics::tooltip_min_width_for(
            m_icon_size),
        natural_width);
}

void DockTooltipWindow::show_tooltip(
    const Glib::ustring &text,
    DockLocation location,
    int tooltip_width,
    const ScreenPosition &position)
{
    // Repeated enter events for the same item must not restart the compositor
    // animation. A position is included because two launchers can share the
    // same application name while occupying different dock slots.
    if (is_current_request(
            text,
            location,
            tooltip_width,
            position))
    {
        return;
    }

    cancel_reveal();
    cancel_opacity_animation();

    // A request that survives the controller's hover delay gets a complete
    // hide/remap/reveal transition. Fast item crossings replace the pending
    // request before reaching this point, so their labels are never shown.
    hide();

    m_has_request = true;
    m_request_text = text;
    m_request_location = location;
    m_request_width = tooltip_width;
    m_request_position = position;

    m_label.set_text(text);

    // This is the same measured width used by DockLayoutEngine. Applying it
    // here prevents the window from changing size after it has been centered.
    set_size_request(
        tooltip_width,
        m_tooltip_height);

    apply_position(
        location,
        position,
        tooltip_width,
        m_tooltip_height);
    m_reveal_timer =
        Glib::signal_timeout().connect(
            [this]()
            {
                // Start partly visible so xfwm4 maps the overlay reliably,
                // then ease both opacity and position into the final state.
                set_opacity(0.18);
                show_all();

                apply_position(
                    m_request_location,
                    m_request_position,
                    m_request_width,
                    m_tooltip_height);
                start_opacity_animation(false);
                return false;
            },
            DockConstants::TOOLTIP_REMAP_DELAY_MS);
}

void DockTooltipWindow::hide_tooltip()
{
    cancel_reveal();
    m_has_request = false;

    if (!get_mapped())
    {
        cancel_opacity_animation();
        hide();
        set_opacity(1.0);
        return;
    }

    start_opacity_animation(true);
}

void DockTooltipWindow::hide_tooltip_immediately()
{
    cancel_reveal();
    cancel_opacity_animation();
    m_has_request = false;
    hide();
    set_opacity(1.0);
}

void DockTooltipWindow::cancel_reveal()
{
    if (m_reveal_timer.connected())
        m_reveal_timer.disconnect();
}

void DockTooltipWindow::cancel_opacity_animation()
{
    if (m_opacity_timer.connected())
        m_opacity_timer.disconnect();
}

void DockTooltipWindow::start_opacity_animation(
    bool hiding)
{
    cancel_opacity_animation();

    m_opacity_animation_hiding = hiding;
    m_opacity_animation_start =
        std::clamp(
            get_opacity(),
            0.0,
            1.0);
    m_opacity_animation_target =
        hiding ? 0.0 : 1.0;
    m_opacity_animation_start_us =
        g_get_monotonic_time();
    m_animation_moves_window =
        !m_uses_layer_shell;

    if (m_animation_moves_window)
    {
        const auto offset =
            animation_offset(
                m_request_location);
        const int visible_x =
            m_monitor_geometry.x +
            m_request_position.x;
        const int visible_y =
            m_monitor_geometry.y +
            m_request_position.y;
        const int hidden_x =
            visible_x + offset.x;
        const int hidden_y =
            visible_y + offset.y;

        if (hiding)
        {
            int current_x = visible_x;
            int current_y = visible_y;
            get_position(current_x, current_y);
            m_animation_start_x = current_x;
            m_animation_start_y = current_y;
            m_animation_target_x = hidden_x;
            m_animation_target_y = hidden_y;
        }
        else
        {
            m_animation_start_x = hidden_x;
            m_animation_start_y = hidden_y;
            m_animation_target_x = visible_x;
            m_animation_target_y = visible_y;
            move(
                m_animation_start_x,
                m_animation_start_y);
        }
    }

    if (std::abs(
            m_opacity_animation_target -
            m_opacity_animation_start) < 0.01)
    {
        advance_opacity_animation();
        return;
    }

    m_opacity_timer =
        Glib::signal_timeout().connect(
            sigc::mem_fun(
                *this,
                &DockTooltipWindow::
                    advance_opacity_animation),
            DockConstants::OVERLAY_ANIMATION_FRAME_MS);
}

bool DockTooltipWindow::advance_opacity_animation()
{
    const double elapsed_ms =
        static_cast<double>(
            g_get_monotonic_time() -
            m_opacity_animation_start_us) /
        1000.0;
    const double progress =
        std::clamp(
            elapsed_ms /
                std::max(
                    1,
                    DockConstants::
                        TOOLTIP_FADE_DURATION_MS),
            0.0,
            1.0);
    const double eased =
        ease_opacity(
            progress,
            m_opacity_animation_hiding);
    const double opacity =
        m_opacity_animation_start +
        (m_opacity_animation_target -
         m_opacity_animation_start) *
            eased;

    set_opacity(
        std::clamp(
            opacity,
            0.0,
            1.0));

    if (m_animation_moves_window)
    {
        const int x = static_cast<int>(
            std::lround(
                m_animation_start_x +
                (m_animation_target_x -
                 m_animation_start_x) *
                    eased));
        const int y = static_cast<int>(
            std::lround(
                m_animation_start_y +
                (m_animation_target_y -
                 m_animation_start_y) *
                    eased));
        move(x, y);
    }

    if (progress < 1.0)
        return true;

    cancel_opacity_animation();
    set_opacity(m_opacity_animation_target);

    if (m_opacity_animation_hiding)
    {
        hide();
        set_opacity(1.0);
    }
    else if (m_animation_moves_window)
    {
        move(
            m_animation_target_x,
            m_animation_target_y);
    }

    return false;
}

void DockTooltipWindow::make_input_transparent()
{
    auto gdk_window = get_window();

    if (!gdk_window)
        return;

    // Tooltips are visual-only overlays. Pass-through plus an empty input
    // region lets pointer events continue to the dock item below instead of
    // causing a leave/hide/re-enter loop when the layer surface maps.
    gdk_window->set_pass_through(true);

    auto empty_region = Cairo::Region::create();

    gdk_window->input_shape_combine_region(
        empty_region,
        0,
        0);
}

bool DockTooltipWindow::is_current_request(
    const Glib::ustring &text,
    DockLocation location,
    int tooltip_width,
    const ScreenPosition &position) const
{
    return m_has_request &&
           m_request_text == text &&
           m_request_location == location &&
           m_request_width == tooltip_width &&
           m_request_position.x == position.x &&
           m_request_position.y == position.y;
}

void DockTooltipWindow::apply_position(
    DockLocation location,
    const ScreenPosition &position,
    int width,
    int height)
{
    if (!m_uses_layer_shell)
    {
        const int global_x =
            m_monitor_geometry.x + position.x;
        const int global_y =
            m_monitor_geometry.y + position.y;

        // GNOME Wayland ignores client-requested toplevel coordinates. The
        // Shell integration consumes this private title payload and moves the
        // tooltip after Mutter creates its surface.
        set_title(
            "Docklight 6 Tooltip@" +
            std::to_string(global_x) + "," +
            std::to_string(global_y));
        move(global_x, global_y);
        return;
    }

    auto window = GTK_WINDOW(gobj());

    const auto layer_position =
        overlay_position_in_workarea(
            position,
            m_workarea_geometry);

    const bool right =
        location == DockLocation::right;

    const bool anchor_bottom =
        location == DockLocation::bottom;

    gtk_layer_set_anchor(
        window,
        GTK_LAYER_SHELL_EDGE_LEFT,
        !right);

    gtk_layer_set_anchor(
        window,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        right);

    gtk_layer_set_anchor(
        window,
        GTK_LAYER_SHELL_EDGE_TOP,
        !anchor_bottom);

    gtk_layer_set_anchor(
        window,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        anchor_bottom);

    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_LEFT,
        right
            ? 0
            : std::max(0, layer_position.x));

    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        right
            ? std::max(
                  0,
                  m_workarea_geometry.width -
                      layer_position.x - width)
            : 0);

    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_TOP,
        anchor_bottom
            ? 0
            : std::max(0, layer_position.y));

    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        anchor_bottom
            ? std::max(
                  0,
                  m_workarea_geometry.height -
                      layer_position.y - height)
            : 0);
}
