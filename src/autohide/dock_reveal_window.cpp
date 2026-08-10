// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_reveal_window.cpp
//
// Implementation overview:
// Implements the transparent layer-shell edge surface that reveals a hidden
// dock.
//
// Important implementation decisions:
// - The surface accepts pointer entry without taking keyboard focus.
// - Monitor anchors and margins follow the current dock placement.
// - A transparent draw handler keeps the trigger visually unobtrusive.
//
// ------------------------------------------------------------

#include "dock_reveal_window.h"
#include "dock/dock_constants.h"

#include <gtk-layer-shell.h>

#include <algorithm>
#include <string>

DockRevealWindow::DockRevealWindow()
{
    set_decorated(false);
    set_resizable(false);
    set_app_paintable(true);
    set_accept_focus(false);
    set_focus_on_map(false);
    set_skip_taskbar_hint(true);
    set_skip_pager_hint(true);
    // Give Mutter an explicit private-surface identity before the first map.
    // The GNOME integration uses the coordinates to place this ordinary
    // Wayland toplevel and the kind to keep it distinct from the main dock.
    set_title("Docklight 6 Reveal@0,0");
    gtk_window_set_role(
        GTK_WINDOW(gobj()),
        "docklight6-reveal");

    add_events(Gdk::ENTER_NOTIFY_MASK);

    signal_draw().connect(
        [](const Cairo::RefPtr<Cairo::Context> &context)
        {
            context->save();
            context->set_operator(Cairo::OPERATOR_SOURCE);
            context->set_source_rgba(0.0, 0.0, 0.0, 0.0);
            context->paint();
            context->restore();
            return true;
        });

    auto *window = GTK_WINDOW(gobj());

    m_uses_layer_shell = gtk_layer_is_supported();

    if (m_uses_layer_shell)
    {
        gtk_layer_init_for_window(window);
        gtk_layer_set_namespace(
            window,
            "docklight6-autohide-reveal");
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
        set_type_hint(Gdk::WINDOW_TYPE_HINT_DOCK);
        set_keep_above(true);
        stick();
        set_position(Gtk::WIN_POS_NONE);

        auto screen = get_screen();
        if (screen)
        {
            auto visual = screen->get_rgba_visual();
            if (visual)
            {
                gtk_widget_set_visual(
                    GTK_WIDGET(gobj()),
                    visual->gobj());
            }
        }
    }

    signal_map().connect(
        [this]()
        {
            if (!m_uses_layer_shell)
                apply_x11_placement();
        });
}

void DockRevealWindow::set_monitor(
    const Glib::RefPtr<Gdk::Monitor> &monitor)
{
    prepare_reconfiguration();
    m_monitor = monitor;

    if (monitor)
    {
        Gdk::Rectangle geometry;
        monitor->get_geometry(geometry);
        m_monitor_geometry = {
            geometry.get_x(),
            geometry.get_y(),
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

void DockRevealWindow::apply_placement(
    const DockPlacement &placement)
{
    prepare_reconfiguration();

    m_placement = placement;
    m_has_placement = true;

    if (!m_uses_layer_shell)
    {
        apply_x11_placement();
        return;
    }

    auto *window = GTK_WINDOW(gobj());

    gtk_layer_set_anchor(
        window,
        GTK_LAYER_SHELL_EDGE_LEFT,
        placement.anchor_left);
    gtk_layer_set_anchor(
        window,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        placement.anchor_right);
    gtk_layer_set_anchor(
        window,
        GTK_LAYER_SHELL_EDGE_TOP,
        placement.anchor_top);
    gtk_layer_set_anchor(
        window,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        placement.anchor_bottom);

    const bool horizontal = placement.is_horizontal();

    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_LEFT,
        horizontal ? placement.margin_left : 0);
    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        horizontal ? placement.margin_right : 0);
    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_TOP,
        horizontal ? 0 : placement.margin_top);
    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        horizontal ? 0 : placement.margin_bottom);

    gtk_widget_set_size_request(
        GTK_WIDGET(window),
        horizontal
            ? placement.width
            : DockConstants::AUTOHIDE_REVEAL_SIZE,
        horizontal
            ? DockConstants::AUTOHIDE_REVEAL_SIZE
            : placement.height);

    gtk_window_resize(window, 1, 1);
}

void DockRevealWindow::prepare_reconfiguration()
{
    // Layer-shell anchors and monitor selection belong to the underlying
    // Wayland surface. Recreate that surface before changing them; immediately
    // remapping the same surface can cause KWin to terminate the client with a
    // protocol error, while changing it in place can retain the old input edge.
    if (get_mapped())
        hide();

    if (m_uses_layer_shell && get_realized())
        gtk_widget_unrealize(GTK_WIDGET(gobj()));
}

void DockRevealWindow::apply_x11_placement()
{
    if (!m_has_placement ||
        m_monitor_geometry.width <= 0 ||
        m_monitor_geometry.height <= 0)
    {
        return;
    }

    const bool horizontal =
        m_placement.is_horizontal();
    const int reveal_size =
        DockConstants::AUTOHIDE_REVEAL_SIZE;

    int width = reveal_size;
    int height = reveal_size;
    int x = m_monitor_geometry.x;
    int y = m_monitor_geometry.y;

    MonitorGeometry workarea =
        m_monitor_geometry;
    if (m_monitor)
    {
        Gdk::Rectangle geometry;
        m_monitor->get_workarea(geometry);
        if (geometry.get_width() > 0 &&
            geometry.get_height() > 0)
        {
            workarea = {
                geometry.get_x(),
                geometry.get_y(),
                geometry.get_width(),
                geometry.get_height()};
        }
    }

    if (horizontal)
    {
        width = m_placement.width > 0
                    ? m_placement.width
                    : std::max(
                          1,
                          m_monitor_geometry.width -
                              m_placement.margin_left -
                              m_placement.margin_right);
        height = reveal_size;

        if (m_placement.anchor_left)
            x += m_placement.margin_left;
        else if (m_placement.anchor_right)
            x += m_monitor_geometry.width -
                m_placement.margin_right - width;
        else
            x += (m_monitor_geometry.width - width) / 2;

        if (m_placement.anchor_top)
            y = workarea.y;
        else if (m_placement.anchor_bottom)
            y = workarea.y + workarea.height - height;
    }
    else
    {
        width = reveal_size;
        height = m_placement.height > 0
                     ? m_placement.height
                     : std::max(
                           1,
                           m_monitor_geometry.height -
                               m_placement.margin_top -
                               m_placement.margin_bottom);

        if (m_placement.anchor_top)
            y += m_placement.margin_top;
        else if (m_placement.anchor_bottom)
            y += m_monitor_geometry.height -
                m_placement.margin_bottom - height;
        else
            y += (m_monitor_geometry.height - height) / 2;

        if (m_placement.anchor_left)
            x = workarea.x;
        else if (m_placement.anchor_right)
            x = workarea.x + workarea.width - width;
    }

    set_size_request(width, height);
    resize(width, height);
    move(x, y);

    // GNOME Wayland ignores ordinary clients' move requests. Its Docklight
    // integration recognizes this coordinate-bearing private-surface title
    // and applies the same target through Mutter before the trigger is used.
    set_title(
        "Docklight 6 Reveal@" +
        std::to_string(x) + "," +
        std::to_string(y));
}

sigc::signal<void> &
DockRevealWindow::signal_reveal_requested()
{
    return m_signal_reveal_requested;
}

bool DockRevealWindow::on_enter_notify_event(
    GdkEventCrossing *event)
{
    if (!event ||
        event->detail != GDK_NOTIFY_INFERIOR)
    {
        m_signal_reveal_requested.emit();
    }

    return true;
}
