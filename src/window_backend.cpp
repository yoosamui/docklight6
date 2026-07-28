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
