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
#include <gdk/gdk.h>
#include <gdk/gdkwayland.h>
#include <glibmm/main.h>

#include <string>

namespace
{

constexpr int X11_EDGE_POLL_INTERVAL_MS = 50;

}

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

    auto *display = gdk_display_get_default();
    if (gtk_layer_is_supported())
        m_backend = SurfaceBackend::layer_shell;
    else if (display && GDK_IS_WAYLAND_DISPLAY(display))
        m_backend = SurfaceBackend::wayland_toplevel;
    else
        m_backend = SurfaceBackend::x11;

    if (m_backend == SurfaceBackend::layer_shell)
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
            if (m_backend == SurfaceBackend::wayland_toplevel)
                apply_wayland_toplevel_placement();
            else if (m_backend == SurfaceBackend::x11)
            {
                apply_x11_placement();
                start_x11_edge_poll();
            }
        });

    signal_unmap().connect(
        [this]()
        {
            stop_x11_edge_poll();
        });
}

DockRevealWindow::~DockRevealWindow()
{
    stop_x11_edge_poll();
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

    if (m_backend == SurfaceBackend::layer_shell)
    {
        gtk_layer_set_monitor(
            GTK_WINDOW(gobj()),
            monitor ? monitor->gobj() : nullptr);
    }
    else if (m_has_placement)
    {
        // X11 and ordinary Wayland reveal surfaces cache absolute geometry.
        // Reapply an unchanged edge placement when the monitor moves or the
        // configured primary output changes.
        if (m_backend == SurfaceBackend::wayland_toplevel)
            apply_wayland_toplevel_placement();
        else
            apply_x11_placement();
    }
}

void DockRevealWindow::apply_placement(
    const DockPlacement &placement)
{
    prepare_reconfiguration();

    m_placement = placement;
    m_has_placement = true;

    if (m_backend == SurfaceBackend::wayland_toplevel)
    {
        apply_wayland_toplevel_placement();
        return;
    }

    if (m_backend == SurfaceBackend::x11)
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

    if (m_backend == SurfaceBackend::layer_shell && get_realized())
        gtk_widget_unrealize(GTK_WIDGET(gobj()));
}

void DockRevealWindow::apply_wayland_toplevel_placement()
{
    if (!m_has_placement ||
        m_monitor_geometry.width <= 0 ||
        m_monitor_geometry.height <= 0)
    {
        return;
    }

    const auto geometry =
        edge_reveal_geometry(
            m_placement,
            m_monitor_geometry,
            DockConstants::AUTOHIDE_REVEAL_SIZE);

    set_size_request(geometry.width, geometry.height);
    resize(geometry.width, geometry.height);

    // GNOME Wayland ignores ordinary clients' move requests. Its Docklight
    // integration recognizes this coordinate-bearing private-surface title
    // and keeps the surface hidden until Mutter commits the target.
    set_title(
        "Docklight 6 Reveal@" +
        std::to_string(geometry.x) + "," +
        std::to_string(geometry.y));
}

void DockRevealWindow::apply_x11_placement()
{
    if (!m_has_placement ||
        m_monitor_geometry.width <= 0 ||
        m_monitor_geometry.height <= 0)
    {
        return;
    }

    const auto geometry =
        edge_reveal_geometry(
            m_placement,
            m_monitor_geometry,
            DockConstants::AUTOHIDE_REVEAL_SIZE);

    set_size_request(geometry.width, geometry.height);
    resize(geometry.width, geometry.height);
    move(geometry.x, geometry.y);
}

void DockRevealWindow::start_x11_edge_poll()
{
    stop_x11_edge_poll();

    if (m_backend != SurfaceBackend::x11 ||
        !m_has_placement)
        return;

    m_pointer_was_on_physical_edge = false;
    m_x11_edge_poll_timer =
        Glib::signal_timeout().connect(
            sigc::mem_fun(
                *this,
                &DockRevealWindow::poll_x11_physical_edge),
            X11_EDGE_POLL_INTERVAL_MS);
}

void DockRevealWindow::stop_x11_edge_poll()
{
    m_x11_edge_poll_timer.disconnect();
    m_pointer_was_on_physical_edge = false;
}

bool DockRevealWindow::poll_x11_physical_edge()
{
    if (!get_mapped() ||
        m_backend != SurfaceBackend::x11 ||
        !m_has_placement)
        return false;

    auto *display = gdk_display_get_default();
    auto *seat = display
        ? gdk_display_get_default_seat(display)
        : nullptr;
    auto *pointer = seat
        ? gdk_seat_get_pointer(seat)
        : nullptr;
    if (!pointer)
        return true;

    int pointer_x = 0;
    int pointer_y = 0;
    gdk_device_get_position(
        pointer,
        nullptr,
        &pointer_x,
        &pointer_y);

    const bool on_edge =
        point_on_physical_reveal_edge(
            m_placement,
            m_monitor_geometry,
            DockConstants::AUTOHIDE_REVEAL_SIZE,
            pointer_x,
            pointer_y);

    if (on_edge && !m_pointer_was_on_physical_edge)
    {
        m_pointer_was_on_physical_edge = true;
        m_signal_reveal_requested.emit();
        return get_mapped();
    }

    m_pointer_was_on_physical_edge = on_edge;
    return true;
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
