// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_home_item.h
//
// Purpose:
// Declares the dock home widget and the global actions exposed by its menu.
//
// Responsibilities:
// - Render and size the home icon.
// - Build and dispatch the dock context menu.
// - Coordinate settings, about, quit, and window actions.
//
// Dependencies and ownership:
// The widget borrows DockWindow and WindowRegistry and owns its GTK
// children and signal connections.
//
// Design notes:
// Application-wide actions are kept separate from launcher-specific
// DockItem behavior.
//
// ------------------------------------------------------------

#pragma once

#include "application/dock_runtime_info.h"

#include <gtkmm.h>
#include <sigc++/connection.h>

#include <string>

class DockWindow;
class WindowRegistry;

class DockHomeItem : public Gtk::EventBox
{
public:
    DockHomeItem(
        DockWindow &dock,
        WindowRegistry *window_registry,
        const DockRuntimeInfo &runtime_info,
        int icon_size,
        const std::string &icon_path);
    ~DockHomeItem() override;

    void set_icon_size(int icon_size);
    void set_icon_path(
        const std::string &icon_path);
    void set_context_menu_corner_radius(
        int corner_radius);

private:
    bool on_enter_notify_event(
        GdkEventCrossing *event) override;
    bool on_leave_notify_event(
        GdkEventCrossing *event) override;
    bool on_button_press_event(
        GdkEventButton *event) override;
    bool on_popup_menu();

    void load_icon_once();
    void update_icon();
    void initialize_context_menu();
    void refresh_context_menu();
    void show_context_menu(
        const GdkEvent *event);
    void schedule_open_settings();

    bool minimize_all();
    bool unminimize_all();
    bool maximize_all();
    bool close_all();

    void open_settings();
    void show_about();
    void exit_docklight();

private:
    DockWindow &m_dock;
    WindowRegistry *m_window_registry = nullptr;
    DockRuntimeInfo m_runtime_info;

    Glib::RefPtr<Gdk::Pixbuf> m_source_icon;
    Glib::RefPtr<Gdk::Pixbuf> m_display_icon;
    Glib::RefPtr<Gtk::CssProvider> m_context_menu_css;

    Gtk::Image m_image;
    Gtk::Menu m_context_menu;

    Gtk::MenuItem m_settings_item;
    Gtk::SeparatorMenuItem m_window_separator;
    Gtk::MenuItem m_minimize_all_item;
    Gtk::MenuItem m_unminimize_all_item;
    Gtk::MenuItem m_maximize_all_item;
    Gtk::SeparatorMenuItem m_close_separator;
    Gtk::MenuItem m_close_all_item;
    Gtk::SeparatorMenuItem m_about_separator;
    Gtk::MenuItem m_about_item;
    Gtk::SeparatorMenuItem m_exit_separator;
    Gtk::MenuItem m_exit_item;

    sigc::connection m_settings_idle;
    sigc::connection m_context_menu_map;
    sigc::connection m_context_menu_unmap;

    std::string m_icon_path;
    int m_icon_size = 0;
    bool m_icon_load_attempted = false;
    bool m_context_menu_mapped = false;
};
