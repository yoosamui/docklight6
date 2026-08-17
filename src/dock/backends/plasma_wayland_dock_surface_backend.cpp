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
#include "presentation/docklight_surface_identity.h"

#include "dock/dock_window.h"
#include "layout/dock_layout_geometry.h"
#include "legacy_dock_surface_backend.h"

#include <gdk/gdkwayland.h>
#include <gtk-layer-shell.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>

namespace
{

bool environment_contains(
    const char *name,
    const std::string &needle)
{
    const auto value = std::getenv(name);
    if (!value)
        return false;

    std::string normalized(value);
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(
                std::tolower(character));
        });

    return normalized.find(needle) !=
           std::string::npos;
}

bool is_kde_wayland_session()
{
    return environment_contains(
               "XDG_SESSION_TYPE",
               "wayland") &&
           (environment_contains(
                "XDG_CURRENT_DESKTOP",
                "kde") ||
            environment_contains(
                "XDG_CURRENT_DESKTOP",
                "plasma") ||
            environment_contains(
                "XDG_SESSION_DESKTOP",
                "kde") ||
            environment_contains(
                "XDG_SESSION_DESKTOP",
                "plasma") ||
            environment_contains(
                "KDE_FULL_SESSION",
                "true"));
}

}

std::unique_ptr<IDockSurfaceBackend>
create_dock_surface_backend(
    DockWindow &window,
    const Glib::RefPtr<Gdk::Monitor> &monitor)
{
    auto *display = gdk_display_get_default();
    const bool plasma_wayland_surface =
        display &&
        GDK_IS_WAYLAND_DISPLAY(display) &&
        is_kde_wayland_session() &&
        gtk_layer_is_supported();

    if (plasma_wayland_surface)
    {
        return std::make_unique<
            PlasmaWaylandDockSurfaceBackend>(
                window,
                monitor);
    }

    return std::make_unique<
        LegacyDockSurfaceBackend>(
            window,
            monitor);
}

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
        DocklightSurfaceIdentity::DOCK_NAMESPACE);
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

MonitorGeometry
PlasmaWaylandDockSurfaceBackend::effective_work_area(
    const MonitorGeometry &,
    const MonitorGeometry &work_area)
{
    return work_area;
}

void PlasmaWaylandDockSurfaceBackend::
    apply_dock_placement(
        const DockPlacement &placement,
    const MonitorGeometry &,
    const MonitorGeometry &)
{
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

bool PlasmaWaylandDockSurfaceBackend::
    uses_native_placement() const
{
    return true;
}

bool PlasmaWaylandDockSurfaceBackend::
    is_native_x11() const
{
    return false;
}

bool PlasmaWaylandDockSurfaceBackend::
    is_ordinary_wayland() const
{
    return false;
}

bool PlasmaWaylandDockSurfaceBackend::
    initial_placement_pending() const
{
    return false;
}

void PlasmaWaylandDockSurfaceBackend::
    complete_initial_placement()
{
}
