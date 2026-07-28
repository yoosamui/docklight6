#pragma once

#include "window_backend.h"

#include <optional>
#include <string>
#include <vector>

class FakeWindowBackend : public WindowBackend
{
public:
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
    bool set_window_icon_geometry(
        const WindowId &window_id,
        const WindowIconGeometry
            &geometry) override;

    void set_snapshot(
        std::vector<ManagedWindow> windows,
        std::vector<WindowId>
            stacking_order,
        std::optional<WindowId>
            active_window);

    void add_window(
        const ManagedWindow &window);
    void update_window(
        const ManagedWindow &window);
    void remove_window(
        const WindowId &window_id);
    void set_active_window(
        const std::optional<WindowId>
            &window_id);
    void set_stacking_order(
        const std::vector<WindowId>
            &stacking_order);
    void set_connected(
        bool connected);
    void set_dock_surface_geometry(
        const std::optional<
            WindowIconGeometry> &geometry);

private:
    ManagedWindow *find_window(
        const WindowId &window_id);

private:
    std::vector<ManagedWindow> m_windows;
    std::vector<WindowId> m_stacking_order;

    std::optional<WindowId> m_active_window;
    std::optional<WindowIconGeometry>
        m_dock_surface_geometry;

    bool m_connected = false;
};
