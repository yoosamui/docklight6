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
        DockHoverEffect hover_effect);
    ~DockItem() override;

    void set_icon_size(int icon_size);
    void set_hover_effect(
        DockHoverEffect effect);
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

    void initialize_context_menu();
    void rebuild_window_menu_items();
    void schedule_window_action(
        const WindowId &window_id,
        bool minimize);
    void show_context_menu(
        const GdkEvent *event);
    void refresh_context_menu();
    void launch_application();
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

    double m_scroll_delta_y = 0.0;

    int m_icon_size = 0;
    int m_zoom_frame = 0;
    int m_zoom_target_frame = 0;
    int m_blur_frame = 0;
    int m_blur_target_frame = 0;

    bool m_hovered = false;
    bool m_single_main_window = false;
};
