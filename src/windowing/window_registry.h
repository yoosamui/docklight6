// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// window_registry.h
//
// Purpose:
// Declares the normalized, queryable window and application model used
// by dock items independently from a concrete window-system backend.
//
// Responsibilities:
// - Mirror backend snapshots and incremental state notifications.
// - Normalize desktop-file identities and group windows by application.
// - Preserve stacking and active-window information for dock policy.
// - Forward supported window actions to the active backend.
//
// Dependencies and ownership:
// The registry borrows WindowBackend and owns its normalized snapshots,
// application groups, identity cache, subscriptions, and public signals.
//
// Design notes:
// Backend transport details end at this boundary; UI consumers observe
// stable application-oriented state.
//
// ------------------------------------------------------------

#pragma once

#include "running_application.h"
#include "window_backend.h"

#include <sigc++/connection.h>
#include <sigc++/signal.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class WindowRegistry
{
public:
    explicit WindowRegistry(
        WindowBackend &backend);
    ~WindowRegistry();

    void start();
    void stop();

    bool connected() const;
    WindowBackendCapabilities
    capabilities() const;

    const std::vector<ManagedWindow> &
    windows() const;
    const std::vector<RunningApplication> &
    running_applications() const;
    const std::optional<WindowId> &
    active_window() const;
    std::optional<WindowIconGeometry>
    dock_surface_geometry() const;
    void set_dock_placement_geometry(
        const std::optional<WindowIconGeometry>
            &geometry);
    void set_dock_hidden(bool hidden);

    const ManagedWindow *find_window(
        const WindowId &window_id) const;
    const RunningApplication *
    find_application(
        const std::string
            &desktop_file_name) const;

    bool activate_window(
        const WindowId &window_id);
    bool present_windows(
        const std::vector<WindowId>
            &window_ids);
    bool hide_windows(
        const std::vector<WindowId>
            &window_ids);
    bool raise_window(
        const WindowId &window_id);
    bool close_window(
        const WindowId &window_id);
    bool set_window_minimized(
        const WindowId &window_id,
        bool minimized);
    bool set_window_maximized(
        const WindowId &window_id,
        bool maximized);
    bool minimize_all();
    bool unminimize_all();
    bool maximize_all();
    bool close_all();
    bool set_window_icon_geometry(
        const WindowId &window_id,
        const WindowIconGeometry &geometry);

    sigc::signal<void> &
    signal_changed();
    sigc::signal<void, bool> &
    signal_connection_changed();
    sigc::signal<void> &
    signal_dock_surface_geometry_changed();
    sigc::signal<void> &
    signal_window_geometry_changed();
    sigc::signal<void> &
    signal_dock_reveal_requested();

private:
    void load_snapshot();
    void clear();
    void rebuild_applications();
    bool apply_stacking_order(
        const std::vector<WindowId>
            &stacking_order);
    bool apply_active_window(
        const std::optional<WindowId>
            &window_id);

    void on_window_added(
        const ManagedWindow &window);
    void on_window_updated(
        const ManagedWindow &window);
    void on_window_removed(
        const WindowId &window_id);
    void on_active_window_changed(
        const std::optional<WindowId>
            &window_id);
    void on_stacking_order_changed(
        const std::vector<WindowId>
            &stacking_order);
    void on_connection_changed(
        bool connected);
    void on_snapshot_changed();

    static std::string
    normalize_desktop_file_name(
        const std::string
            &desktop_file_name);
    std::string canonical_desktop_file_name(
        const ManagedWindow &window);
    static std::optional<std::string>
    installed_desktop_file_name(
        const std::string
            &desktop_file_name);
    static std::string executable_name(
        std::int64_t process_id);

private:
    WindowBackend &m_backend;

    std::vector<ManagedWindow> m_windows;
    std::vector<RunningApplication>
        m_running_applications;

    std::optional<WindowId> m_active_window;

    std::map<
        std::pair<
            std::int64_t,
            std::string>,
        std::string>
        m_canonical_desktop_file_names;

    std::vector<sigc::connection> m_connections;

    sigc::signal<void> m_signal_changed;
    sigc::signal<void, bool>
        m_signal_connection_changed;
    sigc::signal<void>
        m_signal_dock_surface_geometry_changed;
    sigc::signal<void>
        m_signal_window_geometry_changed;
    sigc::signal<void>
        m_signal_dock_reveal_requested;

    bool m_started = false;
    bool m_connected = false;
};
