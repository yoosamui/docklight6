#pragma once

#include "dock_application_controller.h"
#include "dock_layout_types.h"

#include <gtkmm.h>
#include <sigc++/connection.h>

#include <memory>
#include <vector>

class DockWindow;
class WindowRegistry;

class DockItem : public Gtk::EventBox
{
public:
    DockItem(
        DockWindow &dock,
        Glib::RefPtr<Gio::AppInfo> app,
        WindowRegistry *window_registry,
        int icon_size,
        DockHoverEffect hover_effect,
        DockIndicator indicator,
        const std::string
            &indicator_color);
    ~DockItem() override;

    void set_icon_size(int icon_size);
    void set_hover_effect(
        DockHoverEffect effect);
    void set_indicator(
        DockIndicator indicator);
    void set_indicator_color(
        const std::string &color);
    void refresh_indicator();
    void set_context_menu_corner_radius(
        int corner_radius);
    void reload_icon();
    void set_vertical(bool vertical);
    void publish_icon_geometry(
        const WindowIconGeometry &geometry);
    ItemGeometry icon_geometry();

    Glib::ustring app_name() const;

public:
    DockWindow &dock()
    {
        return m_dock;
    }

protected:
    bool on_enter_notify_event(
        GdkEventCrossing *event) override;

    bool on_leave_notify_event(
        GdkEventCrossing *event) override;

    bool on_scroll_event(
        GdkEventScroll *event) override;

private:
    bool on_button_press_event(
        GdkEventButton *event) override;
    bool on_popup_menu();
    bool advance_zoom_animation();
    bool advance_blur_animation();
    bool draw_indicator(
        const Cairo::RefPtr<Cairo::Context>
            &context);

    void initialize_context_menu();
    void rebuild_window_menu_items();
    void schedule_window_action(
        const WindowId &window_id,
        bool minimize);
    void show_context_menu(
        const GdkEvent *event);
    void refresh_context_menu();
    void launch_application();
    void launch_new_window();
    void log_context_action(
        const char *action) const;
    void apply_hover_effect();
    void create_zoom_frames();
    void start_zoom_animation();
    void create_blur_frames();
    void start_blur_animation();

    Glib::RefPtr<Gdk::Pixbuf>
    context_menu_window_icon(
        const std::string &icon_name) const;

    Glib::RefPtr<Gdk::Pixbuf>
    create_standard_hover_pixbuf(
        const Glib::RefPtr<Gdk::Pixbuf>
            &source) const;

private:
    DockWindow &m_dock;

    Glib::RefPtr<Gio::AppInfo> m_app;
    Glib::RefPtr<Gdk::Pixbuf> m_icon_pixbuf;
    Glib::RefPtr<Gdk::Pixbuf> m_hover_pixbuf;
    Glib::RefPtr<Gtk::CssProvider> m_context_menu_css;

    DockApplicationController
        m_application_controller;

    Gtk::Image image;
    Gtk::Label label;
    Gtk::Menu m_context_menu;
    Gtk::SeparatorMenuItem
        m_group_separator;
    Gtk::CheckMenuItem m_attach_item{"A_ttach", true};
    Gtk::SeparatorMenuItem m_attach_separator;
    Gtk::MenuItem m_open_new_window_item{"_Open New Window", true};
    Gtk::SeparatorMenuItem m_window_separator;
    Gtk::MenuItem m_close_all_item{"_Close All", true};
    Gtk::MenuItem m_minimize_item{"M_inimize", true};
    Gtk::MenuItem m_unminimize_item{"_Unminimize", true};
    Gtk::MenuItem m_maximize_item{"M_aximize", true};
    Gtk::SeparatorMenuItem m_close_separator;

    std::vector<Glib::RefPtr<Gdk::Pixbuf>> m_zoom_frames;
    std::vector<Glib::RefPtr<Gdk::Pixbuf>> m_blur_frames;
    std::vector<
        std::unique_ptr<Gtk::MenuItem>>
        m_window_menu_items;

    sigc::connection m_zoom_animation;
    sigc::connection m_blur_animation;
    sigc::connection m_window_action_idle;

    DockHoverEffect m_hover_effect =
        DockHoverEffect::standard;
    DockIndicator m_indicator =
        DockIndicator::lines;

    Gdk::RGBA m_indicator_color;

    double m_scroll_delta_y = 0.0;

    int m_icon_size = 0;
    int m_zoom_frame = 0;
    int m_zoom_target_frame = 0;
    int m_blur_frame = 0;
    int m_blur_target_frame = 0;

    std::size_t m_indicator_window_count = 0;

    bool m_hovered = false;
    bool m_single_main_window = false;
};

class DockHomeItem : public Gtk::EventBox
{
public:
    DockHomeItem(
        DockWindow &dock,
        WindowRegistry *window_registry,
        int icon_size);
    ~DockHomeItem() override;

    void set_icon_size(int icon_size);
    void set_context_menu_corner_radius(
        int corner_radius);

private:
    bool on_button_press_event(
        GdkEventButton *event) override;
    bool on_popup_menu();

    void load_icon_once();
    void update_icon();
    void initialize_context_menu();
    void refresh_context_menu();
    void show_context_menu(
        const GdkEvent *event);

    bool minimize_all();
    bool unminimize_all();
    bool maximize_all();
    bool close_all();

    void open_settings();
    void show_about();
    void exit_docklight();

private:
    DockWindow &m_dock;
    WindowRegistry *m_window_registry =
        nullptr;

    Glib::RefPtr<Gdk::Pixbuf>
        m_source_icon;
    Glib::RefPtr<Gdk::Pixbuf>
        m_display_icon;
    Glib::RefPtr<Gtk::CssProvider>
        m_context_menu_css;

    Gtk::Image m_image;
    Gtk::Menu m_context_menu;

    Gtk::MenuItem
        m_settings_item{"_Settings", true};
    Gtk::SeparatorMenuItem
        m_window_separator;
    Gtk::MenuItem
        m_minimize_all_item{
            "_Minimize all",
            true};
    Gtk::MenuItem
        m_unminimize_all_item{
            "_Unminimize all",
            true};
    Gtk::MenuItem
        m_maximize_all_item{
            "Ma_ximize all",
            true};
    Gtk::SeparatorMenuItem
        m_close_separator;
    Gtk::MenuItem
        m_close_all_item{
            "_Close all",
            true};
    Gtk::SeparatorMenuItem
        m_about_separator;
    Gtk::MenuItem
        m_about_item{"A_bout", true};
    Gtk::SeparatorMenuItem
        m_exit_separator;
    Gtk::MenuItem
        m_exit_item{"_Exit", true};

    sigc::connection m_settings_idle;

    int m_icon_size = 0;
    bool m_icon_load_attempted = false;
};
