// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// layer_shell_dock_surface_backend.cpp
//
// Implementation overview:
// Owns gtk-layer-shell setup, placement, monitor selection, and exclusive
// zone handling for the main dock surface on native Wayland compositors.
//
// ------------------------------------------------------------

#include "layer_shell_dock_surface_backend.h"
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
    const bool native_layer_surface =
        display &&
        GDK_IS_WAYLAND_DISPLAY(display) &&
        gtk_layer_is_supported();

    if (native_layer_surface)
    {
        return std::make_unique<
            LayerShellDockSurfaceBackend>(
                window,
                monitor);
    }

    return std::make_unique<
        LegacyDockSurfaceBackend>(
            window,
            monitor);
}

LayerShellDockSurfaceBackend::
    LayerShellDockSurfaceBackend(
        DockWindow &window,
        const Glib::RefPtr<Gdk::Monitor>
            &monitor)
    : m_window(window),
      m_monitor(monitor),
      m_plasma_session(
          is_kde_wayland_session())
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

void LayerShellDockSurfaceBackend::set_monitor(
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
LayerShellDockSurfaceBackend::
    output_geometry() const
{
    DockLayoutGeometry geometry;
    return geometry.output_geometry(m_monitor);
}

MonitorGeometry
LayerShellDockSurfaceBackend::work_area() const
{
    DockLayoutGeometry geometry;
    return geometry.monitor_geometry(m_monitor);
}

MonitorGeometry
LayerShellDockSurfaceBackend::effective_work_area(
    const MonitorGeometry &,
    const MonitorGeometry &work_area)
{
    return work_area;
}

void LayerShellDockSurfaceBackend::
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

void LayerShellDockSurfaceBackend::reserve_space(
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

void LayerShellDockSurfaceBackend::
    clear_reserved_space()
{
    gtk_layer_set_exclusive_zone(
        GTK_WINDOW(m_window.gobj()),
        0);
}

bool LayerShellDockSurfaceBackend::
    uses_native_placement() const
{
    return true;
}

bool LayerShellDockSurfaceBackend::
    is_native_x11() const
{
    return false;
}

bool LayerShellDockSurfaceBackend::
    is_ordinary_wayland() const
{
    return false;
}

DockAutohideEffect
LayerShellDockSurfaceBackend::
    default_autohide_effect() const
{
    return m_plasma_session
               ? DockAutohideEffect::plasma
               : DockAutohideEffect::slide;
}

std::vector<DockAutohideEffect>
LayerShellDockSurfaceBackend::
    configurable_autohide_effects() const
{
    if (m_plasma_session)
    {
        return {
            DockAutohideEffect::plasma,
            DockAutohideEffect::slide};
    }

    return {DockAutohideEffect::slide};
}

bool LayerShellDockSurfaceBackend::
    delegates_autohide_effect(
        DockAutohideEffect) const
{
    return false;
}

double LayerShellDockSurfaceBackend::
    autohide_fade_opacity() const
{
    return m_window.get_opacity();
}

void LayerShellDockSurfaceBackend::
    set_autohide_fade_opacity(
        double opacity)
{
    m_window.set_opacity(opacity);
}

void LayerShellDockSurfaceBackend::
    finish_autohide_fade(
        bool hidden)
{
    if (hidden)
        m_window.hide();
}

bool LayerShellDockSurfaceBackend::
    supports_autohide_slide() const
{
    return true;
}

double LayerShellDockSurfaceBackend::
    autohide_slide_progress() const
{
    return m_autohide_slide_progress;
}

void LayerShellDockSurfaceBackend::
    set_autohide_slide_progress(
        const DockPlacement &placement,
        double progress)
{
    const double clamped =
        std::clamp(progress, 0.0, 1.0);
    const int width = placement.width > 0
        ? placement.width
        : std::max(
              1,
              m_window.get_allocated_width());
    const int height = placement.height > 0
        ? placement.height
        : std::max(
              1,
              m_window.get_allocated_height());

    const auto offset =
        autohide_slide_content_offset(
            placement,
            width,
            height,
            clamped);

    m_autohide_slide_progress = clamped;
    m_window.set_surface_horizontal_offset(
        offset.x);
    m_window.set_surface_vertical_offset(
        offset.y);
    // Plasma's Slide effect is deliberately movement-only.
    m_window.set_opacity(1.0);
}

void LayerShellDockSurfaceBackend::
    finish_autohide_slide(
        bool)
{
    // Keep the fully clipped layer surface mapped. Remapping it on reveal
    // makes KWin animate the surface while DockLight simultaneously moves
    // its contents, producing the visible deviation this effect must avoid.
}

bool LayerShellDockSurfaceBackend::
    initial_placement_pending() const
{
    return false;
}

void LayerShellDockSurfaceBackend::
    complete_initial_placement()
{
}
