// ------------------------------------------------------------
// Docklight 6.0
//
// Hyprland application-window integration.
// ------------------------------------------------------------

#pragma once

#include "windowing/window_backend.h"

#include <glib.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct HyprlandSnapshot
{
    std::vector<ManagedWindow> windows;
    std::vector<WindowId> stacking_order;
    std::optional<WindowId> active_window;
    std::map<WindowId, std::string> addresses;
};

HyprlandSnapshot parse_hyprland_snapshot(
    const std::string &clients_json,
    const std::string &monitors_json,
    const std::string &active_window_json);

class HyprlandWindowBackend final
    : public WindowBackend
{
public:
    using QueryHandler = std::function<
        std::optional<std::string>(
            const std::vector<std::string> &)>;
    using CommandHandler = std::function<
        bool(const std::vector<std::string> &)>;

    HyprlandWindowBackend();
    HyprlandWindowBackend(
        QueryHandler query_handler,
        CommandHandler command_handler);
    ~HyprlandWindowBackend() override;

    void start() override;
    void stop() override;

    std::string name() const override;
    WindowBackendCapabilities capabilities() const override;
    bool connected() const override;
    std::vector<ManagedWindow> windows() const override;
    std::vector<WindowId> stacking_order() const override;
    std::optional<WindowId> active_window() const override;
    std::optional<WindowIconGeometry>
    dock_surface_geometry() const override;

    bool activate_window(const WindowId &window_id) override;
    bool present_windows(
        const std::vector<WindowId> &window_ids) override;
    bool hide_windows(
        const std::vector<WindowId> &window_ids) override;
    bool raise_window(const WindowId &window_id) override;
    bool close_window(const WindowId &window_id) override;
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
        const WindowIconGeometry &geometry) override;

private:
    void refresh();
    bool dispatch_lua(const std::string &expression);
    bool dispatch_legacy(
        const std::string &dispatcher,
        const std::string &arguments);
    std::string selector(const WindowId &window_id) const;
    void connect_event_socket();
    void disconnect_event_socket();
    void schedule_refresh();

    static gboolean on_event_socket(
        gint fd,
        GIOCondition condition,
        gpointer data);
    static gboolean on_refresh_timeout(gpointer data);

private:
    QueryHandler m_query_handler;
    CommandHandler m_command_handler;
    HyprlandSnapshot m_snapshot;
    bool m_started = false;
    bool m_connected = false;
    bool m_lua_dispatch = false;
    bool m_manage_event_socket = false;
    int m_event_fd = -1;
    unsigned int m_event_source = 0;
    unsigned int m_refresh_source = 0;
};
