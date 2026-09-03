// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_item.h
//
// Purpose:
// Declares interactive application launcher items.
//
// Responsibilities:
// - Render launcher icons, indicators, hover effects, and menus.
// - Translate pointer, scroll, and drag events into dock actions.
// - Publish launcher icon geometry for compositor effects.
// - Provide global window and settings actions through the home item.
//
// Dependencies and ownership:
// Items are GTK widgets owned by their container. They borrow DockWindow
// and WindowRegistry while owning launcher presentation and interaction.
//
// Design notes:
// Window-group policy is delegated to DockApplicationController and
// dock-wide layout and ordering remain DockWindow responsibilities.
//
// ------------------------------------------------------------

#pragma once

#include "application/dock_application_controller.h"
#include "layout/dock_layout_types.h"

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
        const std::string &desktop_id,
        bool attached,
        WindowRegistry *window_registry,
        int icon_size,
        DockHoverEffect hover_effect,
        DockIndicator indicator,
        const std::string
            &indicator_color);

    // Subclass constructor for items that are not backed by one installed
    // application, such as a saved Session. The identifiers are the desktop
    // IDs whose windows the item groups, so indicator, previews, and the
    // minimize/maximize/close actions keep working unchanged.
    DockItem(
        DockWindow &dock,
        const std::string &desktop_id,
        std::vector<std::string>
            application_identifiers,
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
    void set_manage_all_workspaces(bool enabled);
    void refresh_indicator();
    void set_context_menu_corner_radius(
        int corner_radius);
    // Overridden by items whose image does not come from a Gio::AppInfo.
    virtual void reload_icon();
    void set_vertical(bool vertical);
    void set_attached(bool attached);
    void publish_icon_geometry(
        const WindowIconGeometry &geometry);
    ItemGeometry icon_geometry();

    virtual Glib::ustring app_name() const;
    Glib::ustring tooltip_text() const;
    std::vector<ApplicationWindowEntry>
    window_entries() const;
    bool show_window(const WindowId &window_id);
    bool minimize_window(const WindowId &window_id);
    bool close_window(const WindowId &window_id);
    bool toggle_window(const WindowId &window_id);
    const std::string &desktop_id() const
    {
        return m_desktop_id;
    }
    bool attached() const
    {
        return m_attached;
    }
    bool running() const
    {
        return m_application_controller
            .running();
    }

public:
    DockWindow &dock()
    {
        return m_dock;
    }

protected:
    // Every identity a window may report for one application. Subclasses that
    // group several applications build their identifier list from this.
    // include_icon_names widens matching to the entry's themed icon names.
    // That helps a single application whose windows report an icon name, but
    // generic names such as "text-editor" are shared by unrelated programs, so
    // an item that groups several applications leaves them out.
    static std::vector<std::string>
    application_identifiers(
        const Glib::RefPtr<Gio::AppInfo> &app,
        const std::string &desktop_id,
        bool include_icon_names = true);

    // Applies one already-sized image and rebuilds every pixbuf derived from
    // it, so a subclass supplying its own image keeps the ordinary hover and
    // animation behaviour.
    void apply_icon_pixbuf(
        const Glib::RefPtr<Gdk::Pixbuf> &pixbuf);
    void initialize(
        int icon_size,
        const std::string &indicator_color);

    void set_application_identifiers(
        std::vector<std::string>
            application_identifiers);
    void set_window_filter(
        std::function<bool(const ManagedWindow &)>
            window_filter);
    int icon_size() const
    {
        return m_icon_size;
    }

    virtual bool has_edit_action() const;
    virtual void edit_item();
    virtual Glib::RefPtr<Gdk::Pixbuf>
    context_menu_entry_icon(
        const ApplicationWindowEntry &entry) const;

    // The dynamic rows above the static actions, and what activating one does.
    // An ordinary item lists the live windows of its application group and
    // shows or minimizes the chosen window. An item whose content does not
    // come from the window registry, such as a Session, supplies its own rows
    // and defines its own activation.
    virtual std::vector<ApplicationWindowEntry>
    context_menu_entries() const;
    virtual void
    activate_context_menu_entry(
        const ApplicationWindowEntry &entry);

    // Dispatching a window command has to outlive GtkMenu's popup grab, so a
    // subclass that routes its own rows to a window must reuse this rather
    // than calling the controller directly.
    void schedule_window_action(
        const WindowId &window_id,
        bool minimize);

    bool on_enter_notify_event(
        GdkEventCrossing *event) override;

    bool on_leave_notify_event(
        GdkEventCrossing *event) override;

    bool on_button_release_event(
        GdkEventButton *event) override;
    bool on_scroll_event(
        GdkEventScroll *event) override;

private:
    bool on_button_press_event(
        GdkEventButton *event) override;
    void on_drag_begin(
        const Glib::RefPtr<
            Gdk::DragContext> &context) override;
    void on_drag_end(
        const Glib::RefPtr<
            Gdk::DragContext> &context) override;
    void on_drag_data_get(
        const Glib::RefPtr<
            Gdk::DragContext> &context,
        Gtk::SelectionData &selection_data,
        guint info,
        guint time) override;
    bool on_drag_motion(
        const Glib::RefPtr<
            Gdk::DragContext> &context,
        int x,
        int y,
        guint time) override;
    bool on_drag_drop(
        const Glib::RefPtr<
            Gdk::DragContext> &context,
        int x,
        int y,
        guint time) override;
    bool on_popup_menu();
    bool advance_primary_action_effect();
    bool advance_zoom_animation();
    bool advance_blur_animation();
    bool draw_indicator(
        const Cairo::RefPtr<Cairo::Context>
            &context);

    void initialize_context_menu();
    void rebuild_window_menu_items();
    void show_context_menu(
        const GdkEvent *event);
    void refresh_context_menu();
    void start_primary_action_effect();
    void perform_primary_action();
    // Overridden by items that start something other than one application.
    virtual void launch_application();
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
        const std::string &icon_name,
        const std::string &desktop_file_name)
        const;
    Glib::RefPtr<Gdk::Pixbuf>
    context_menu_minimized_icon() const;

private:
    DockWindow &m_dock;

    Glib::RefPtr<Gio::AppInfo> m_app;
    std::string m_desktop_id;
    Glib::RefPtr<Gdk::Pixbuf> m_icon_pixbuf;
    Glib::RefPtr<Gdk::Pixbuf> m_hover_pixbuf;
    Glib::RefPtr<Gdk::Pixbuf> m_drag_pixbuf;
    Glib::RefPtr<Gtk::CssProvider> m_context_menu_css;

    DockApplicationController
        m_application_controller;

    Gtk::Image image;
    Gtk::Label label;
    Gtk::Menu m_context_menu;
    Gtk::MenuItem m_edit_item;
    Gtk::MenuItem m_remove_item;
    Gtk::SeparatorMenuItem
        m_group_separator;
    Gtk::CheckMenuItem m_attach_item;
    Gtk::SeparatorMenuItem m_attach_separator;
    Gtk::MenuItem m_open_new_window_item;
    Gtk::SeparatorMenuItem m_window_separator;
    Gtk::MenuItem m_close_all_item;
    Gtk::MenuItem m_minimize_item;
    Gtk::MenuItem m_unminimize_item;
    Gtk::MenuItem m_maximize_item;
    Gtk::SeparatorMenuItem m_close_separator;

    std::vector<Glib::RefPtr<Gdk::Pixbuf>> m_zoom_frames;
    std::vector<Glib::RefPtr<Gdk::Pixbuf>> m_blur_frames;
    std::vector<
        std::unique_ptr<Gtk::MenuItem>>
        m_window_menu_items;
    std::vector<WindowId>
        m_window_menu_order;

    sigc::connection m_zoom_animation;
    sigc::connection m_blur_animation;
    sigc::connection m_primary_action_effect;
    sigc::connection m_window_action_idle;
    sigc::connection m_context_menu_button_press;
    sigc::connection m_context_menu_map;
    sigc::connection m_context_menu_unmap;
    sigc::connection m_context_menu_uninhibit_idle;

    DockHoverEffect m_hover_effect =
        DockHoverEffect::standard;
    DockIndicator m_indicator =
        DockIndicator::lines;

    Gdk::RGBA m_indicator_color;

    double m_scroll_delta_y = 0.0;
    gint64 m_last_primary_action_time = 0;

    int m_icon_size = 0;
    int m_zoom_frame = 0;
    int m_zoom_target_frame = 0;
    int m_blur_frame = 0;
    int m_blur_target_frame = 0;
    int m_primary_action_effect_frame = 0;
    int m_drag_hot_x = 0;
    int m_drag_hot_y = 0;

    std::size_t m_indicator_window_count = 0;

    bool m_hovered = false;
    bool m_attached = false;
    bool m_updating_attach_state = false;
    bool m_single_main_window = false;
    bool m_primary_button_pressed = false;
    bool m_dragging = false;
    bool m_context_menu_mapped = false;
    bool m_context_menu_secondary_dismissed = false;
    bool m_context_menu_window_action_pending = false;
};
