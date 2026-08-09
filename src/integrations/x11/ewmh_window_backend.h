// ------------------------------------------------------------
// Docklight 6.0
//
// Generic EWMH/X11 window backend implemented through libwnck.
// ------------------------------------------------------------

#pragma once

#include "windowing/window_backend.h"

#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#include <libwnck/libwnck.h>

class EwmhWindowBackend : public WindowBackend
{
public:
    ~EwmhWindowBackend() override;

    void start() override;
    void stop() override;

    std::string name() const override;
    WindowBackendCapabilities capabilities() const override;
    bool connected() const override;

    std::vector<ManagedWindow> windows() const override;
    std::vector<WindowId> stacking_order() const override;
    std::optional<WindowId> active_window() const override;
    std::optional<WindowIconGeometry> dock_surface_geometry() const override;

    bool activate_window(const WindowId &window_id) override;
    bool present_windows(const std::vector<WindowId> &window_ids) override;
    bool hide_windows(const std::vector<WindowId> &window_ids) override;
    bool raise_window(const WindowId &window_id) override;
    bool close_window(const WindowId &window_id) override;
    bool set_window_minimized(const WindowId &window_id, bool minimized) override;
    bool set_window_maximized(const WindowId &window_id, bool maximized) override;
    bool set_window_icon_geometry(const WindowId &window_id,
                                  const WindowIconGeometry &geometry) override;

protected:
    explicit EwmhWindowBackend(
        std::string backend_name);

    virtual std::optional<bool>
    activate_windows_override(
        const std::vector<WnckWindow *> &windows);

    virtual std::optional<bool>
    set_window_minimized_override(
        WnckWindow *window,
        bool minimized);

private:
    static void on_window_opened(WnckScreen *, WnckWindow *, gpointer data);
    static void on_window_closed(WnckScreen *, WnckWindow *, gpointer data);
    static void on_screen_changed(WnckScreen *, gpointer data);
    static void on_screen_value_changed(WnckScreen *, gpointer, gpointer data);
    static void on_active_workspace_changed(
        WnckScreen *, WnckWorkspace *, gpointer data);
    static void on_window_changed(WnckWindow *, gpointer data);
    static void on_window_state_changed(WnckWindow *,
                                        WnckWindowState,
                                        WnckWindowState,
                                        gpointer data);

    void watch_window(WnckWindow *window);
    void snapshot_changed();
    bool defer_activation_until_workspace(
        const std::vector<WindowId> &window_ids,
        WnckWindow *target,
        guint32 timestamp);
    void complete_pending_activation();
    WnckWindow *find_window(const WindowId &window_id) const;
    static WindowId window_id(WnckWindow *window);
    static ManagedWindow managed_window(WnckWindow *window,
                                        WnckScreen *screen);

    WnckHandle *m_handle = nullptr;
    WnckScreen *m_screen = nullptr;
    std::string m_backend_name;
    std::vector<WindowId> m_pending_activation_window_ids;
    int m_pending_activation_workspace = -1;
    guint32 m_pending_activation_timestamp = 0;
    bool m_started = false;
    bool m_connected = false;
};
