// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// window_backend.cpp
//
// Implementation overview:
// Implements WindowBackend's transport-neutral signal accessors and
// protected notification helpers.
//
// Important implementation decisions:
// - Concrete backends decide when state changes.
// - This layer delivers normalized notifications without adding policy.
// - WindowRegistry and other subscribers share the same signal surface.
//
// ------------------------------------------------------------

#include "window_backend.h"

std::optional<WindowIconGeometry>
WindowBackend::dock_workarea_geometry() const
{
    return m_dock_workarea_geometry;
}

void WindowBackend::set_dock_workarea_geometry(
    const std::optional<WindowIconGeometry>
        &geometry)
{
    if (m_dock_workarea_geometry == geometry)
        return;

    m_dock_workarea_geometry = geometry;
    m_signal_dock_workarea_geometry_changed
        .emit();
}

std::optional<WindowIconGeometry>
WindowBackend::dock_placement_geometry() const
{
    return m_dock_placement_geometry;
}

void WindowBackend::set_dock_placement_geometry(
    const std::optional<WindowIconGeometry>
        &geometry)
{
    if (m_dock_placement_geometry == geometry)
        return;

    m_dock_placement_geometry = geometry;
    m_signal_dock_placement_geometry_changed
        .emit();
}

sigc::signal<
    void,
    const ManagedWindow &> &
WindowBackend::signal_window_added()
{
    return m_signal_window_added;
}

sigc::signal<
    void,
    const ManagedWindow &> &
WindowBackend::signal_window_updated()
{
    return m_signal_window_updated;
}

sigc::signal<
    void,
    const WindowId &> &
WindowBackend::signal_window_removed()
{
    return m_signal_window_removed;
}

sigc::signal<
    void,
    const std::optional<WindowId> &> &
WindowBackend::signal_active_window_changed()
{
    return m_signal_active_window_changed;
}

sigc::signal<
    void,
    const std::vector<WindowId> &> &
WindowBackend::signal_stacking_order_changed()
{
    return m_signal_stacking_order_changed;
}

sigc::signal<
    void,
    bool> &
WindowBackend::signal_connection_changed()
{
    return m_signal_connection_changed;
}

sigc::signal<void> &
WindowBackend::signal_snapshot_changed()
{
    return m_signal_snapshot_changed;
}

sigc::signal<void> &
WindowBackend::
    signal_dock_surface_geometry_changed()
{
    return m_signal_dock_surface_geometry_changed;
}

sigc::signal<void> &
WindowBackend::
    signal_dock_workarea_geometry_changed()
{
    return m_signal_dock_workarea_geometry_changed;
}

sigc::signal<void> &
WindowBackend::
    signal_dock_placement_geometry_changed()
{
    return m_signal_dock_placement_geometry_changed;
}

sigc::signal<void> &
WindowBackend::signal_dock_reveal_requested()
{
    return m_signal_dock_reveal_requested;
}

void WindowBackend::notify_window_added(
    const ManagedWindow &window)
{
    m_signal_window_added.emit(
        window);
}

void WindowBackend::notify_window_updated(
    const ManagedWindow &window)
{
    m_signal_window_updated.emit(
        window);
}

void WindowBackend::notify_window_removed(
    const WindowId &window_id)
{
    m_signal_window_removed.emit(
        window_id);
}

void WindowBackend::notify_active_window_changed(
    const std::optional<WindowId>
        &window_id)
{
    m_signal_active_window_changed.emit(
        window_id);
}

void WindowBackend::notify_stacking_order_changed(
    const std::vector<WindowId>
        &stacking_order)
{
    m_signal_stacking_order_changed.emit(
        stacking_order);
}

void WindowBackend::notify_connection_changed(
    bool connected)
{
    m_signal_connection_changed.emit(
        connected);
}

void WindowBackend::notify_snapshot_changed()
{
    m_signal_snapshot_changed.emit();
}

void WindowBackend::
    notify_dock_surface_geometry_changed()
{
    m_signal_dock_surface_geometry_changed.emit();
}

bool WindowBackend::dock_hidden() const
{
    return m_dock_hidden;
}

void WindowBackend::set_dock_hidden(bool hidden)
{
    if (m_dock_hidden == hidden)
        return;

    m_dock_hidden = hidden;
    m_signal_dock_hidden_changed.emit(hidden);
}

sigc::signal<void, bool> &
WindowBackend::signal_dock_hidden_changed()
{
    return m_signal_dock_hidden_changed;
}

sigc::signal<void, bool> &
WindowBackend::signal_dock_pointer_inside_changed()
{
    return m_signal_dock_pointer_inside_changed;
}

sigc::signal<void, bool> &
WindowBackend::signal_preview_pointer_inside_changed()
{
    return m_signal_preview_pointer_inside_changed;
}

sigc::signal<void, bool> &
WindowBackend::signal_preview_input_forwarding_changed()
{
    return m_signal_preview_input_forwarding_changed;
}

sigc::signal<void, const WindowId &> &
WindowBackend::signal_preview_window_activated()
{
    return m_signal_preview_window_activated;
}

sigc::signal<void, bool> &
WindowBackend::signal_dock_animation_completed()
{
    return m_signal_dock_animation_completed;
}
