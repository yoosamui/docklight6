// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// plasma_wayland_dock_surface_backend.cpp
//
// Implementation overview:
// Owns gtk-layer-shell setup, placement, monitor selection, and exclusive
// zone handling for the main dock surface on native Plasma Wayland.
//
// ------------------------------------------------------------

#include "plasma_wayland_dock_surface_backend.h"

#include "dock/dock_window.h"
#include "layout/dock_layout_geometry.h"

#include <gtk-layer-shell.h>

PlasmaWaylandDockSurfaceBackend::
    PlasmaWaylandDockSurfaceBackend(
        DockWindow &window,
        const Glib::RefPtr<Gdk::Monitor>
            &monitor)
    : m_window(window),
      m_monitor(monitor)
{
    auto *gtk_window =
        GTK_WINDOW(m_window.gobj());

    gtk_layer_init_for_window(gtk_window);
    gtk_layer_set_keyboard_mode(
        gtk_window,
        GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_namespace(
        gtk_window,
        "docklight6");
    gtk_layer_set_layer(
        gtk_window,
        GTK_LAYER_SHELL_LAYER_TOP);

    set_monitor(monitor);
}

void PlasmaWaylandDockSurfaceBackend::set_monitor(
    const Glib::RefPtr<Gdk::Monitor>
        &monitor)
{
    m_monitor = monitor;

    gtk_layer_set_monitor(
        GTK_WINDOW(m_window.gobj()),
        monitor
            ? monitor->gobj()
            : nullptr);
}

MonitorGeometry
PlasmaWaylandDockSurfaceBackend::
    output_geometry() const
{
    DockLayoutGeometry geometry;
    return geometry.output_geometry(m_monitor);
}

MonitorGeometry
PlasmaWaylandDockSurfaceBackend::work_area() const
{
    DockLayoutGeometry geometry;
    return geometry.monitor_geometry(m_monitor);
}

void PlasmaWaylandDockSurfaceBackend::
    apply_dock_placement(
        const DockPlacement &placement,
        const MonitorGeometry &,
        const MonitorGeometry &)
{
    m_window.apply_visual_style();
    m_window.apply_dock_orientation(
        placement.orientation);

    auto *gtk_window =
        GTK_WINDOW(m_window.gobj());

    gtk_layer_set_anchor(
        gtk_window,
        GTK_LAYER_SHELL_EDGE_LEFT,
        placement.anchor_left);
    gtk_layer_set_anchor(
        gtk_window,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        placement.anchor_right);
    gtk_layer_set_anchor(
        gtk_window,
        GTK_LAYER_SHELL_EDGE_TOP,
        placement.anchor_top);
    gtk_layer_set_anchor(
        gtk_window,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        placement.anchor_bottom);

    gtk_layer_set_margin(
        gtk_window,
        GTK_LAYER_SHELL_EDGE_LEFT,
        placement.margin_left);
    gtk_layer_set_margin(
        gtk_window,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        placement.margin_right);
    gtk_layer_set_margin(
        gtk_window,
        GTK_LAYER_SHELL_EDGE_TOP,
        placement.margin_top);
    gtk_layer_set_margin(
        gtk_window,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        placement.margin_bottom);

    // gtk-layer-shell uses the GTK widget request as the surface's natural
    // size. Request a fresh configure after an orientation or size change.
    gtk_widget_set_size_request(
        GTK_WIDGET(gtk_window),
        placement.width,
        placement.height);
    gtk_window_resize(gtk_window, 1, 1);

    reserve_space(placement);
}

void PlasmaWaylandDockSurfaceBackend::reserve_space(
    const DockPlacement &placement)
{
    auto *gtk_window =
        GTK_WINDOW(m_window.gobj());

    if (placement.exclusive_zone < 0)
    {
        gtk_layer_auto_exclusive_zone_enable(
            gtk_window);
    }
    else
    {
        gtk_layer_set_exclusive_zone(
            gtk_window,
            placement.exclusive_zone);
    }
}

void PlasmaWaylandDockSurfaceBackend::
    clear_reserved_space()
{
    gtk_layer_set_exclusive_zone(
        GTK_WINDOW(m_window.gobj()),
        0);
}
