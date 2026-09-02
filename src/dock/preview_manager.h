// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// preview_manager.h
//
// Purpose:
// Declares dock-item preview intent, surface lifetime, and window actions.
//
// Responsibilities:
// - Schedule and display window-group previews for hovered dock items.
// - Coordinate thumbnail policy, live refresh, and cache priming.
// - Route preview activation, reload, and close requests.
// - Maintain preview-related autohide inhibition and pointer state.
//
// Dependencies and ownership:
// PreviewManager borrows DockWindow, autohide, and tooltip services. It owns
// the preview window, media monitor, timers, and signal connections.
//
// Design notes:
// Delayed previews wait for a fully revealed dock so placement uses stable
// surface coordinates.
//
// ------------------------------------------------------------

#pragma once

#include "config/dock_configuration.h"
#include "application/dock_application_controller.h"
#include "layout/dock_layout_engine.h"
#include "layout/dock_layout_geometry.h"
#include "windowing/window_backend.h"

#include <gdkmm/monitor.h>
#include <sigc++/connection.h>
#include <sigc++/signal.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

class DockAutohideController;
class DockItem;
class DockMediaPlaybackMonitor;
class DockPreviewWindow;
class DockWindow;
class TooltipManager;

class PreviewManager
{
public:
    PreviewManager(
        DockWindow &window,
        DockAutohideController &autohide,
        TooltipManager &tooltips,
        const DockSettings &settings,
        const DockLayoutRequest &layout_request,
        const Glib::RefPtr<Gdk::Monitor> &monitor,
        std::function<ScreenPosition()> dock_position,
        std::function<bool()> is_dock_pointer_inside);
    ~PreviewManager();

    void schedule_show(DockItem &item, int show_delay_ms);
    void hide(bool cancel_pending_show = true);
    void hide_immediately();
    void cancel_show_timer();

    void apply_configuration(const DockSettings &settings);
    void set_layout_request(const DockLayoutRequest &request);
    void set_layout_geometry(
        const MonitorGeometry &usable_monitor,
        const MonitorGeometry &output);
    void set_monitor(const Glib::RefPtr<Gdk::Monitor> &monitor);
    void set_thumbnail_policy(WindowThumbnailPolicy policy);
    void set_rounded_corners(bool enabled, int radius);
    void set_input_forwarding(bool forwarding);
    void set_shell_pointer_inside(bool inside);
    void prime_cache(
        const std::vector<ApplicationWindowEntry> &entries);

    bool has_request_for(const DockItem &item) const;
    bool pointer_inside() const;
    bool input_forwarding() const;
    const std::string &desktop_id() const;

    void activate_window(const WindowId &window_id);

    sigc::signal<void> &signal_pointer_entered();
    sigc::signal<void> &signal_pointer_left();

private:
    void show_pending_if_ready();
    void show_now(
        DockItem &item,
        const WindowId &excluded_window_id = {});
    void reload_thumbnail(const WindowId &window_id);
    void close_window(
        const WindowId &window_id,
        bool last_card);

private:
    DockWindow &m_window;
    DockAutohideController &m_autohide;
    TooltipManager &m_tooltips;
    std::unique_ptr<DockPreviewWindow> m_preview_window;
    std::unique_ptr<DockMediaPlaybackMonitor> m_media_monitor;
    DockLayoutEngine m_layout_engine;
    DockLayoutGeometry m_layout_geometry;
    std::function<ScreenPosition()> m_dock_position;
    std::function<bool()> m_is_dock_pointer_inside;

    DockSettings m_settings;
    DockLayoutRequest m_layout_request;
    MonitorGeometry m_usable_monitor_geometry;
    MonitorGeometry m_output_geometry;
    Glib::RefPtr<Gdk::Monitor> m_monitor;

    std::string m_pending_desktop_id;
    std::string m_preview_desktop_id;
    sigc::connection m_show_timer;
    sigc::connection m_fully_revealed;
    sigc::connection m_media_playback_changed;
    sigc::connection m_input_forwarding_reset;
    bool m_inhibits_autohide = false;
    bool m_pointer_inside = false;
    bool m_shell_pointer_state_known = false;
    bool m_shell_pointer_inside = false;
    bool m_input_forwarding = false;
    bool m_show_delay_elapsed = false;
    bool m_last_card_close_pending = false;

    sigc::signal<void> m_signal_pointer_entered;
    sigc::signal<void> m_signal_pointer_left;
};
