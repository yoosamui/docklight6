#pragma once

#include <gtkmm.h>
#include "dock_types.h"

class PopupWindow : public Gtk::Window
{
public:
    PopupWindow();

    // void show_popup();
    void show_popup(int center_y);

    void hide_popup();

    void move_relative_to_dock(
        DockLocation location,
        int dock_x,
        int dock_y,
        int dock_width,
        int dock_height,
        int item_center_x,
        int item_center_y);

    void set_horizontal_position(int x);

    void set_application_name(
        const std::string &name);

protected:
    void on_size_allocate(Gtk::Allocation &allocation) override;

private:
    int m_pending_y = -1;
    int m_pending_x = -1;

private:
    Gtk::Box m_box;
    Gtk::Image m_image;
    Gtk::Label m_title;

    Gtk::Label m_label;
};