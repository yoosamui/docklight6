#pragma once

#include "dock_layout_types.h"

#include <gtkmm.h>

class DockWindow;

class DockItem : public Gtk::EventBox
{
public:
    DockItem(
        DockWindow &dock,
        Glib::RefPtr<Gio::AppInfo> app,
        int icon_size,
        DockHoverEffect hover_effect);

    void set_icon_size(int icon_size);
    void set_hover_effect(
        DockHoverEffect effect);
    void reload_icon();
    void set_vertical(bool vertical);
    Glib::ustring app_name() const;

public:
    DockWindow &dock() { return m_dock; }

protected:
    bool on_enter_notify_event(
        GdkEventCrossing *event) override;

    bool on_leave_notify_event(
        GdkEventCrossing *event) override;

private:
    DockWindow &m_dock;
    Glib::RefPtr<Gio::AppInfo> m_app;

    Gtk::Image image;
    Gtk::Label label;
    Glib::RefPtr<Gdk::Pixbuf> m_icon_pixbuf;
    Glib::RefPtr<Gdk::Pixbuf> m_hover_pixbuf;
    int m_icon_size = 0;
    DockHoverEffect m_hover_effect =
        DockHoverEffect::standard;
    bool m_hovered = false;

    bool on_button_press_event(
        GdkEventButton *event) override;
    void apply_hover_effect();
    Glib::RefPtr<Gdk::Pixbuf>
    create_standard_hover_pixbuf(
        const Glib::RefPtr<Gdk::Pixbuf>
            &source) const;
};
