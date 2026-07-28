#pragma once

#include "managed_window.h"

#include <sigc++/signal.h>

#include <optional>
#include <string>
#include <vector>

struct WindowBackendCapabilities
{
    bool can_activate = false;
    bool can_raise = false;
    bool can_close = false;
    bool can_minimize = false;
    bool can_maximize = false;
    bool provides_stacking_order = false;
    bool provides_activities = false;
    bool provides_virtual_desktops = false;
    bool provides_frame_geometry = false;
    bool provides_icons = false;
};

class WindowBackend
{
public:
    virtual ~WindowBackend() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual std::string name() const = 0;
    virtual WindowBackendCapabilities
    capabilities() const = 0;
    virtual bool connected() const = 0;

    virtual std::vector<ManagedWindow>
    windows() const = 0;
    virtual std::vector<WindowId>
    stacking_order() const = 0;
    virtual std::optional<WindowId>
    active_window() const = 0;

    virtual bool activate_window(
        const WindowId &window_id) = 0;
    virtual bool raise_window(
        const WindowId &window_id) = 0;
    virtual bool close_window(
        const WindowId &window_id) = 0;
    virtual bool set_window_minimized(
        const WindowId &window_id,
        bool minimized) = 0;
    virtual bool set_window_maximized(
        const WindowId &window_id,
        bool maximized) = 0;

    sigc::signal<
        void,
        const ManagedWindow &> &
    signal_window_added();

    sigc::signal<
        void,
        const ManagedWindow &> &
    signal_window_updated();

    sigc::signal<
        void,
        const WindowId &> &
    signal_window_removed();

    sigc::signal<
        void,
        const std::optional<WindowId> &> &
    signal_active_window_changed();

    sigc::signal<
        void,
        const std::vector<WindowId> &> &
    signal_stacking_order_changed();

    sigc::signal<
        void,
        bool> &
    signal_connection_changed();

    sigc::signal<void> &
    signal_snapshot_changed();

protected:
    void notify_window_added(
        const ManagedWindow &window);
    void notify_window_updated(
        const ManagedWindow &window);
    void notify_window_removed(
        const WindowId &window_id);
    void notify_active_window_changed(
        const std::optional<WindowId>
            &window_id);
    void notify_stacking_order_changed(
        const std::vector<WindowId>
            &stacking_order);
    void notify_connection_changed(
        bool connected);
    void notify_snapshot_changed();

private:
    sigc::signal<
        void,
        const ManagedWindow &>
        m_signal_window_added;
    sigc::signal<
        void,
        const ManagedWindow &>
        m_signal_window_updated;
    sigc::signal<
        void,
        const WindowId &>
        m_signal_window_removed;
    sigc::signal<
        void,
        const std::optional<WindowId> &>
        m_signal_active_window_changed;
    sigc::signal<
        void,
        const std::vector<WindowId> &>
        m_signal_stacking_order_changed;
    sigc::signal<
        void,
        bool>
        m_signal_connection_changed;

    sigc::signal<void>
        m_signal_snapshot_changed;
};
