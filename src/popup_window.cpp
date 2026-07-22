#include "popup_window.h"

#include <gtk-layer-shell.h>

#include <iostream>

namespace
{
    constexpr int POPUP_VERTICAL_OFFSET = -50;
}

PopupWindow::PopupWindow()
{
    set_default_size(200, 100);

    m_label.set_text("WAYLAND POPUP TEST");
    m_label.set_size_request(200, 100);

    add(m_label);

    signal_realize().connect(
        [this]()
        {
            gtk_layer_init_for_window(
                GTK_WINDOW(gobj()));

            gtk_layer_set_layer(
                GTK_WINDOW(gobj()),
                GTK_LAYER_SHELL_LAYER_OVERLAY);

            gtk_layer_set_keyboard_mode(
                GTK_WINDOW(gobj()),
                GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        });

    show_all();
}

void PopupWindow::on_size_allocate(
    Gtk::Allocation &allocation)
{
    Gtk::Window::on_size_allocate(allocation);

    if (m_pending_y >= 0)
    {
        GtkWindow *win = GTK_WINDOW(gobj());

        gtk_layer_set_margin(
            win,
            GTK_LAYER_SHELL_EDGE_TOP,
            m_pending_y);

        std::cout
            << "REALLOCATE popup TOP="
            << m_pending_y
            << std::endl;

        m_pending_y = -1;
    }
}

void PopupWindow::set_application_name(
    const std::string &name)
{
    m_label.set_text(name);
    m_label.show();
}

void PopupWindow::show_popup(int center_y)
{
    int popup_height = 100;

    int y = center_y - popup_height / 2;

    gtk_layer_set_anchor(
        GTK_WINDOW(gobj()),
        GTK_LAYER_SHELL_EDGE_LEFT,
        true);

    gtk_layer_set_anchor(
        GTK_WINDOW(gobj()),
        GTK_LAYER_SHELL_EDGE_TOP,
        true);

    gtk_layer_set_margin(
        GTK_WINDOW(gobj()),
        GTK_LAYER_SHELL_EDGE_LEFT,
        100);

    gtk_layer_set_margin(
        GTK_WINDOW(gobj()),
        GTK_LAYER_SHELL_EDGE_TOP,
        y);

    std::cout
        << "Popup requested y="
        << y
        << std::endl;

    show();
}

void PopupWindow::hide_popup()
{

    GtkWindow *win = GTK_WINDOW(gobj());

    hide();

    gtk_layer_set_margin(
        win,
        GTK_LAYER_SHELL_EDGE_TOP,
        0);

    gtk_layer_set_margin(
        win,
        GTK_LAYER_SHELL_EDGE_LEFT,
        0);
}

void PopupWindow::move_relative_to_dock(
    DockLocation location,
    int dock_x,
    int dock_y,
    int dock_width,
    int dock_height,
    int item_center_x,
    int item_center_y)
{
    GtkWindow *win = GTK_WINDOW(gobj());

    switch (location)
    {
    case DockLocation::left:
    {
        gtk_layer_set_anchor(
            win,
            GTK_LAYER_SHELL_EDGE_LEFT,
            TRUE);

        gtk_layer_set_anchor(
            win,
            GTK_LAYER_SHELL_EDGE_RIGHT,
            FALSE);

        gtk_layer_set_anchor(
            win,
            GTK_LAYER_SHELL_EDGE_TOP,
            TRUE);

        gtk_layer_set_anchor(
            win,
            GTK_LAYER_SHELL_EDGE_BOTTOM,
            FALSE);

        gtk_layer_set_margin(
            win,
            GTK_LAYER_SHELL_EDGE_LEFT,
            dock_width + 10);

        gtk_layer_set_margin(
            win,
            GTK_LAYER_SHELL_EDGE_TOP,
            item_center_y - 50);

        std::cout
            << "LEFT popup top="
            << item_center_y - 50
            << std::endl;

        break;
    }
    case DockLocation::bottom:
    {
        gtk_layer_set_anchor(
            win,
            GTK_LAYER_SHELL_EDGE_BOTTOM,
            TRUE);

        gtk_layer_set_anchor(
            win,
            GTK_LAYER_SHELL_EDGE_TOP,
            FALSE);

        gtk_layer_set_anchor(
            win,
            GTK_LAYER_SHELL_EDGE_LEFT,
            TRUE);

        gtk_layer_set_anchor(
            win,
            GTK_LAYER_SHELL_EDGE_RIGHT,
            FALSE);

        gtk_layer_set_margin(
            win,
            GTK_LAYER_SHELL_EDGE_BOTTOM,
            dock_height + POPUP_VERTICAL_OFFSET);

        gtk_layer_set_margin(
            win,
            GTK_LAYER_SHELL_EDGE_LEFT,
            dock_x + item_center_x - 100);

        break;
    }
    default:
        break;
    }

    show();

    std::cout
        << "Popup shown"
        << std::endl;
}

void PopupWindow::set_horizontal_position(int y)
{
    GtkWindow *win = GTK_WINDOW(gobj());

    gtk_layer_set_margin(
        win,
        GTK_LAYER_SHELL_EDGE_TOP,
        y);

    gtk_layer_set_margin(
        win,
        GTK_LAYER_SHELL_EDGE_LEFT,
        100);
}
// void PopupWindow::set_horizontal_position(int y)
// {
//     GtkWindow *win = GTK_WINDOW(gobj());

//     std::cout
//         << "SET MARGIN TOP="
//         << y
//         << std::endl;

//     gtk_layer_set_margin(
//         win,
//         GTK_LAYER_SHELL_EDGE_TOP,
//         y);

//     gtk_layer_set_margin(
//         win,
//         GTK_LAYER_SHELL_EDGE_LEFT,
//         100);

//     gtk_layer_set_anchor(
//         win,
//         GTK_LAYER_SHELL_EDGE_TOP,
//         TRUE);

//     gtk_layer_set_anchor(
//         win,
//         GTK_LAYER_SHELL_EDGE_LEFT,
//         TRUE);
// }
