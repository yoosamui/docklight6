// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
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

#include <gtk-layer-shell.h>

#include <algorithm>
#include <glibmm/main.h>
#include <pangomm/layout.h>

DockTooltipWindow::DockTooltipWindow()
{
    set_decorated(false);
    set_resizable(false);
    set_accept_focus(false);

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

    gtk_layer_init_for_window(window);
    gtk_layer_set_keyboard_interactivity(window, FALSE);
    gtk_layer_set_namespace(window, "docklight6-tooltip");
    gtk_layer_set_layer(window, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_exclusive_zone(window, 0);

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
    gtk_layer_set_monitor(
        GTK_WINDOW(gobj()),
        monitor
            ? monitor->gobj()
            : nullptr);
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

    // Update layer-shell placement while the tooltip is unmapped, then reveal
    // it again below. The hide/show transition is what lets KWin apply the
    // configured tooltip effect for each dock item.
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

    apply_position(location, position);

    m_reveal_timer =
        Glib::signal_timeout().connect(
            [this]()
            {
                show_all();
                return false;
            },
            DockConstants::TOOLTIP_REMAP_DELAY_MS);
}

void DockTooltipWindow::hide_tooltip()
{
    cancel_reveal();
    m_has_request = false;
    hide();
}

void DockTooltipWindow::cancel_reveal()
{
    if (m_reveal_timer.connected())
        m_reveal_timer.disconnect();
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
    const ScreenPosition &position)
{
    auto window = GTK_WINDOW(gobj());

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
        right ? 0 : position.x);

    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        right ? position.x : 0);

    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_TOP,
        anchor_bottom ? 0 : position.y);

    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        anchor_bottom ? position.y : 0);
}
