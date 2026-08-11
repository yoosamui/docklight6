// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// window_backend.h
//
// Purpose:
// Declares the desktop-neutral window-system boundary used by the dock.
//
// Responsibilities:
// - Expose backend capabilities and normalized window snapshots.
// - Accept window actions without leaking transport-specific APIs.
// - Notify WindowRegistry of state, connection, and geometry changes.
//
// Dependencies and ownership:
// Concrete backends own native integration state. WindowBackend owns
// only its signals and does not own returned window-system objects.
//
// Design notes:
// Capability flags keep higher layers from assuming every desktop
// integration supports the same state or actions.
//
// ------------------------------------------------------------

#pragma once

#include "managed_window.h"
#include "window_icon_geometry.h"

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
    bool accepts_icon_geometry = false;
    bool provides_dock_reveal_trigger = false;
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
    virtual std::optional<WindowIconGeometry>
    dock_surface_geometry() const = 0;

    std::optional<WindowIconGeometry>
    dock_placement_geometry() const;
    void set_dock_placement_geometry(
        const std::optional<WindowIconGeometry>
            &geometry);
    bool dock_hidden() const;
    void set_dock_hidden(bool hidden);

    virtual bool activate_window(
        const WindowId &window_id) = 0;
    virtual bool present_windows(
        const std::vector<WindowId>
            &window_ids) = 0;
    virtual bool hide_windows(
        const std::vector<WindowId>
            &window_ids) = 0;
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
    virtual bool set_window_icon_geometry(
        const WindowId &window_id,
        const WindowIconGeometry &geometry) = 0;

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
    sigc::signal<void> &
    signal_dock_surface_geometry_changed();
    sigc::signal<void> &
    signal_dock_placement_geometry_changed();
    sigc::signal<void> &
    signal_dock_reveal_requested();
    sigc::signal<void, bool> &
    signal_dock_hidden_changed();
    sigc::signal<void, bool> &
    signal_dock_pointer_inside_changed();
    sigc::signal<void, bool> &
    signal_dock_animation_completed();

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
    void notify_dock_surface_geometry_changed();

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
    sigc::signal<void>
        m_signal_dock_surface_geometry_changed;
    sigc::signal<void>
        m_signal_dock_placement_geometry_changed;
    sigc::signal<void>
        m_signal_dock_reveal_requested;
    sigc::signal<void, bool>
        m_signal_dock_hidden_changed;
    sigc::signal<void, bool>
        m_signal_dock_pointer_inside_changed;
    sigc::signal<void, bool>
        m_signal_dock_animation_completed;
    std::optional<WindowIconGeometry>
        m_dock_placement_geometry;
    bool m_dock_hidden = false;
};
