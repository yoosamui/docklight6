// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_preview_window.h
//
// Purpose:
// Declares the interactive window-group preview surface shown from a dock
// item.
//
// Responsibilities:
// - Build and update preview cards for managed windows.
// - Position the surface relative to dock placement and monitor bounds.
// - Coordinate thumbnails, live streams, and window actions.
//
// Dependencies and ownership:
// The window borrows DockApplicationController and owns its GTK widgets,
// providers, cached frames, and signals.
//
// Design notes:
// Transport-specific capture details remain behind provider interfaces.
//
// ------------------------------------------------------------

#pragma once

#include "application/dock_application_controller.h"
#include "layout/dock_layout_types.h"
#include "dock_window_stream_provider.h"
#include "dock_window_thumbnail_provider.h"

#include <gdkmm/monitor.h>
#include <gtkmm.h>
#include <sigc++/signal.h>

#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

struct DockPreviewSize
{
    int width = 0;
    int height = 0;
    int card_width = 0;
    int gap = 0;
    int padding = 0;
    int header_height = 0;
};

class DockPreviewWindow : public Gtk::Window
{
public:
    static constexpr int CARD_WIDTH = 160;
    static constexpr int CARD_USER_HEIGHT = 0;
    static constexpr int MIN_HEIGHT = 64;
    static constexpr int MAX_HEIGHT = 512;
    static constexpr int CARD_GAP = 8;
    static constexpr int WINDOW_PADDING = 8;

    DockPreviewWindow();
    ~DockPreviewWindow() override;

    void set_monitor(
        const Glib::RefPtr<Gdk::Monitor> &monitor);
    void set_card_user_height(int height);
    void set_rounded_corners(
        bool enabled,
        int radius);
    void prime_thumbnail_cache(
        const std::vector<ApplicationWindowEntry>
            &entries);

    DockPreviewSize preferred_size(
        const std::vector<ApplicationWindowEntry>
            &entries,
        int available_width,
        int available_height) const;

    void show_preview(
        const std::vector<ApplicationWindowEntry>
            &entries,
        DockLocation location,
        const ScreenPosition &position,
        const DockPreviewSize &size);
    void hide_preview();
    void hide_preview_immediately();
    void set_dynamic_refresh(
        bool enabled,
        const std::string &media_title = {});

    bool visible_for(const WindowId &window_id) const;

    sigc::signal<void> &signal_pointer_entered();
    sigc::signal<void> &signal_pointer_left();
    sigc::signal<void, const WindowId &> &
    signal_activate_window();
    sigc::signal<void, const WindowId &> &
    signal_reload_thumbnail();
    sigc::signal<void, const WindowId &> &
    signal_close_window();

protected:
    bool on_enter_notify_event(
        GdkEventCrossing *event) override;
    bool on_leave_notify_event(
        GdkEventCrossing *event) override;

private:
    struct ThumbnailTarget
    {
        Gtk::Image *image = nullptr;
        std::string fallback_icon;
        int fallback_size = 0;
        int target_width = 0;
        int target_height = 0;
        std::string caption;
        bool active = false;
        bool minimized = false;
        bool on_current_desktop = false;
        bool application_auxiliary = false;
        bool capture_in_flight = false;
        bool has_thumbnail = false;
        bool has_live_signature = false;
        std::uint64_t live_signature = 0;
        bool probe_in_flight = false;
        bool has_probe_signature = false;
        std::uint64_t probe_signature = 0;
        std::int64_t live_until_us = 0;
        unsigned int initial_capture_failures = 0;
    };

    void rebuild(
        const std::vector<ApplicationWindowEntry>
            &entries,
        const DockPreviewSize &size);
    void request_thumbnail(
        const WindowId &window_id,
        unsigned int generation);
    void request_cached_thumbnail(
        const WindowId &window_id,
        unsigned int retries_remaining);
    void request_active_cache_refresh(
        const WindowId &window_id);
    void persist_thumbnail_cache(
        const WindowId &window_id,
        const Glib::RefPtr<Gdk::Pixbuf>
            &thumbnail);
    void show_thumbnail_fallback(
        const WindowId &window_id);
    void start_next_thumbnail_recovery();
    void request_live_x11_thumbnail(
        const WindowId &window_id,
        unsigned int generation,
        bool allow_xfwm_group_fallback = false);
    void request_x11_change_probe(
        const WindowId &window_id,
        unsigned int generation);
    void start_live_streams();
    void stop_live_streams();
    void clear_cards();
    void cancel_opacity_animation();
    void start_opacity_animation(bool hiding);
    bool advance_opacity_animation();
    void apply_position(
        DockLocation location,
        const ScreenPosition &position,
        int width,
        int height);

private:
    DockWindowThumbnailProvider
        m_thumbnail_provider;
    DockWindowStreamProvider
        m_stream_provider;
    Glib::RefPtr<Gtk::CssProvider> m_css;
    Glib::RefPtr<Gtk::CssProvider>
        m_corner_css;

    Gtk::EventBox m_surface;
    Gtk::ScrolledWindow m_scroller;
    Gtk::Box m_row{
        Gtk::ORIENTATION_HORIZONTAL,
        CARD_GAP};

    std::vector<Gtk::EventBox *> m_cards;
    std::map<WindowId, ThumbnailTarget>
        m_thumbnail_targets;
    std::map<WindowId, Glib::RefPtr<Gdk::Pixbuf>>
        m_thumbnail_cache;
    std::map<WindowId, std::string>
        m_thumbnail_cache_keys;
    std::set<WindowId>
        m_thumbnail_cache_persisted;
    std::set<WindowId>
        m_thumbnail_cache_dirty;
    std::set<WindowId>
        m_thumbnail_cache_in_flight;
    std::set<WindowId>
        m_known_window_ids;
    std::set<WindowId>
        m_thumbnail_cache_eligible;
    std::set<WindowId>
        m_thumbnail_cache_active;
    std::map<WindowId, sigc::connection>
        m_thumbnail_cache_retries;
    sigc::connection
        m_thumbnail_cache_refresh;
    std::set<WindowId>
        m_thumbnail_recovery_requested;
    std::deque<WindowId>
        m_thumbnail_recovery_queue;
    WindowId m_thumbnail_recovery_active;
    std::set<WindowId>
        m_thumbnail_recovery_capture_allowed;
    sigc::connection
        m_thumbnail_recovery_delay;
    std::vector<WindowId> m_window_ids;
    sigc::connection m_x11_live_refresh;
    sigc::connection m_x11_probe_refresh;
    sigc::connection m_opacity_timer;

    sigc::signal<void> m_pointer_entered;
    sigc::signal<void> m_pointer_left;
    sigc::signal<void, const WindowId &>
        m_activate_window;
    sigc::signal<void, const WindowId &>
        m_reload_thumbnail;
    sigc::signal<void, const WindowId &>
        m_close_window;

    unsigned int m_generation = 0;
    int m_card_user_height = CARD_USER_HEIGHT;
    gint64 m_opacity_animation_start_us = 0;
    double m_opacity_animation_start = 1.0;
    double m_opacity_animation_target = 1.0;
    int m_animation_start_x = 0;
    int m_animation_start_y = 0;
    int m_animation_target_x = 0;
    int m_animation_target_y = 0;
    std::string m_media_title;
    std::set<WindowId> m_live_window_ids;
    MonitorGeometry m_monitor_geometry;
    ScreenPosition m_position;
    DockLocation m_location = DockLocation::bottom;
    DockPreviewSize m_size;
    bool m_dynamic_refresh = false;
    bool m_has_position = false;
    bool m_animation_moves_window = false;
    bool m_opacity_animation_hiding = false;
    bool m_uses_layer_shell = false;
};
