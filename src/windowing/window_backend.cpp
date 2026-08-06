// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// Implements WindowBackend's transport-neutral signal accessors and
// protected notification helpers.
//
// Concrete backends decide when state changes; this file only delivers
// those changes to WindowRegistry and other subscribers.
//
// ------------------------------------------------------------

#include "window_backend.h"

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
