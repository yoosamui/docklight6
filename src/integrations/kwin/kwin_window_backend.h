// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// kwin_window_backend.h
//
// Purpose:
// Declares the WindowBackend implementation backed by versioned state
// supplied by the Docklight KWin script.
//
// Responsibilities:
// - Stage and atomically commit complete window snapshots.
// - Accept ordered incremental updates and current-desktop changes.
// - Dispatch requested window actions through an injected handler.
// - Advertise capabilities and notify generic backend consumers.
//
// Dependencies and ownership:
// The backend owns its snapshots and handlers. Transport objects are
// owned by KWinIntegrationService and communicate through this API.
//
// Design notes:
// Revision checks prevent stale D-Bus messages from corrupting state,
// while staged snapshots prevent consumers from seeing partial refreshes.
//
// ------------------------------------------------------------

#pragma once

#include "kwin_window_command.h"
#include "windowing/window_backend.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

class KWinWindowBackend : public WindowBackend
{
public:
    using IconGeometryHandler =
        std::function<
            bool(
                const WindowId &,
                const WindowIconGeometry &)>;

    void start() override;
    void stop() override;

    std::string name() const override;
    WindowBackendCapabilities
    capabilities() const override;
    bool connected() const override;

    std::vector<ManagedWindow>
    windows() const override;
    std::vector<WindowId>
    stacking_order() const override;
    std::optional<WindowId>
    active_window() const override;
    std::optional<WindowIconGeometry>
    dock_surface_geometry() const override;

    bool activate_window(
        const WindowId &window_id) override;
    bool present_windows(
        const std::vector<WindowId>
            &window_ids) override;
    bool hide_windows(
        const std::vector<WindowId>
            &window_ids) override;
    bool raise_window(
        const WindowId &window_id) override;
    bool close_window(
        const WindowId &window_id) override;
    bool set_window_minimized(
        const WindowId &window_id,
        bool minimized) override;
    bool set_window_maximized(
        const WindowId &window_id,
        bool maximized) override;
    bool place_window(
        const WindowId &window_id,
        const WindowPlacement &placement) override;
    bool set_window_icon_geometry(
        const WindowId &window_id,
        const WindowIconGeometry
            &geometry) override;

    void set_command_handler(
        KWinWindowCommandHandler handler);
    void set_icon_geometry_handler(
        IconGeometryHandler handler);

    bool register_integration(
        std::uint32_t protocol_version);
    void unregister_integration();

    bool begin_snapshot(
        std::uint64_t revision);
    bool stage_window(
        std::uint64_t revision,
        const ManagedWindow &window);
    bool commit_snapshot(
        std::uint64_t revision,
        const std::optional<WindowId>
            &active_window,
        const std::vector<WindowId>
            &stacking_order);
    void cancel_snapshot();

    bool publish_window(
        std::uint64_t revision,
        const ManagedWindow &window);
    bool publish_window_removed(
        std::uint64_t revision,
        const WindowId &window_id);
    bool publish_active_window(
        std::uint64_t revision,
        const std::optional<WindowId>
            &window_id);
    bool publish_stacking_order(
        std::uint64_t revision,
        const std::vector<WindowId>
            &stacking_order);
    bool publish_current_desktop(
        std::uint64_t revision,
        const std::string &desktop_id,
        unsigned int desktop_number);
    bool publish_dock_surface_geometry(
        std::uint64_t revision,
        const std::optional<
            WindowIconGeometry> &geometry);
    bool publish_dock_workarea_geometry(
        std::uint64_t revision,
        const std::optional<
            WindowIconGeometry> &geometry);

    std::uint64_t last_revision() const;

private:
    ManagedWindow *find_window(
        const WindowId &window_id);
    const ManagedWindow *find_window(
        const WindowId &window_id) const;

    bool accepts_incremental_revision(
        std::uint64_t revision) const;
    void apply_current_desktop(
        ManagedWindow &window) const;
    bool dispatch_command(
        KWinWindowCommandType type,
        const WindowId &window_id,
        bool state = false);
    void clear_state();

private:
    std::vector<ManagedWindow> m_windows;
    std::vector<ManagedWindow>
        m_staged_windows;
    std::vector<WindowId> m_stacking_order;

    std::optional<WindowId> m_active_window;
    std::optional<WindowIconGeometry>
        m_dock_surface_geometry;
    std::optional<std::uint64_t>
        m_staged_revision;
    std::string m_current_desktop_id;
    unsigned int m_current_desktop_number = 0;

    KWinWindowCommandHandler
        m_command_handler;

    IconGeometryHandler
        m_icon_geometry_handler;

    std::uint64_t m_last_revision = 0;
    std::uint32_t
        m_registered_protocol_version = 0;

    bool m_started = false;
    bool m_registered = false;
    bool m_connected = false;
};
