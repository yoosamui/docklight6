#pragma once

#include <gtkmm.h>

class DockWindow;

class DockItem : public Gtk::EventBox
{
public:
    DockItem(
        DockWindow &dock,
        Glib::RefPtr<Gio::AppInfo> app);

    void set_icon_size(int icon_size);
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
    int m_icon_size = 0;

    bool on_button_press_event(
        GdkEventButton *event) override;
};
