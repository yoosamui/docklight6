// Interactive horizontal window-group preview surface.

#pragma once

#include "dock_application_controller.h"
#include "layout/dock_layout_types.h"
#include "dock_window_stream_provider.h"
#include "dock_window_thumbnail_provider.h"

#include <gdkmm/monitor.h>
#include <gtkmm.h>
#include <sigc++/signal.h>

#include <cstdint>
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
    void set_dynamic_refresh(
        bool enabled,
        const std::string &media_title = {});

    bool visible_for(const WindowId &window_id) const;

    sigc::signal<void> &signal_pointer_entered();
    sigc::signal<void> &signal_pointer_left();
    sigc::signal<void, const WindowId &> &
    signal_activate_window();
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
        bool capture_in_flight = false;
        bool has_thumbnail = false;
        bool has_live_signature = false;
        std::uint64_t live_signature = 0;
    };

    void rebuild(
        const std::vector<ApplicationWindowEntry>
            &entries,
        const DockPreviewSize &size);
    void request_thumbnail(
        const WindowId &window_id,
        unsigned int generation);
    void start_live_streams();
    void clear_cards();
    void apply_position(
        DockLocation location,
        const ScreenPosition &position);

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
    std::vector<WindowId> m_window_ids;

    sigc::signal<void> m_pointer_entered;
    sigc::signal<void> m_pointer_left;
    sigc::signal<void, const WindowId &>
        m_activate_window;
    sigc::signal<void, const WindowId &>
        m_close_window;

    unsigned int m_generation = 0;
    int m_card_user_height = CARD_USER_HEIGHT;
    std::string m_media_title;
    std::set<WindowId> m_live_window_ids;
    bool m_dynamic_refresh = false;
};
