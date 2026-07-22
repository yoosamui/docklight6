// #include "dock_tooltip.h"

#include <gtk-layer-shell.h>

DockTooltip::DockTooltip()
    : Gtk::Window(Gtk::WINDOW_TOPLEVEL)
{
    set_decorated(false);
    set_resizable(false);

    GtkWindow *gtk_win = GTK_WINDOW(gobj());

    gtk_layer_init_for_window(gtk_win);

    gtk_layer_set_namespace(
        gtk_win,
        "docklight6-tooltip");

    gtk_layer_set_layer(
        gtk_win,
        GTK_LAYER_SHELL_LAYER_OVERLAY);

    gtk_layer_set_exclusive_zone(
        gtk_win,
        0);

    add(m_label);

    m_label.set_margin_start(8);
    m_label.set_margin_end(8);
    m_label.set_margin_top(4);
    m_label.set_margin_bottom(4);

    show_all_children();

    hide();

  
}

void DockTooltip::set_text(const Glib::ustring &text)
{
    m_label.set_text(text);
}

void DockTooltip::show(
    const Glib::ustring &text,
    const TooltipLayout &layout)
{
    set_text(text);

    GtkWindow *gtk_win = GTK_WINDOW(gobj());

    // Reset every anchor first
    gtk_layer_set_anchor(gtk_win, GTK_LAYER_SHELL_EDGE_TOP, FALSE);
    gtk_layer_set_anchor(gtk_win, GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);
    gtk_layer_set_anchor(gtk_win, GTK_LAYER_SHELL_EDGE_LEFT, FALSE);
    gtk_layer_set_anchor(gtk_win, GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);

    switch (layout.location)
    {
    case DockLocation::bottom:

        gtk_layer_set_anchor(
            gtk_win,
            GTK_LAYER_SHELL_EDGE_BOTTOM,
            TRUE);

        gtk_layer_set_anchor(
            gtk_win,
            GTK_LAYER_SHELL_EDGE_LEFT,
            TRUE);

        gtk_layer_set_margin(
            gtk_win,
            GTK_LAYER_SHELL_EDGE_LEFT,
            layout.offset);

        gtk_layer_set_margin(
            gtk_win,
            GTK_LAYER_SHELL_EDGE_BOTTOM,
            layout.distance);

        break;

    case DockLocation::top:

        gtk_layer_set_anchor(
            gtk_win,
            GTK_LAYER_SHELL_EDGE_TOP,
            TRUE);

        gtk_layer_set_anchor(
            gtk_win,
            GTK_LAYER_SHELL_EDGE_LEFT,
            TRUE);

        gtk_layer_set_margin(
            gtk_win,
            GTK_LAYER_SHELL_EDGE_LEFT,
            layout.offset);

        gtk_layer_set_margin(
            gtk_win,
            GTK_LAYER_SHELL_EDGE_TOP,
            layout.distance);

        break;

    case DockLocation::left:

        gtk_layer_set_anchor(
            gtk_win,
            GTK_LAYER_SHELL_EDGE_LEFT,
            TRUE);

        gtk_layer_set_anchor(
            gtk_win,
            GTK_LAYER_SHELL_EDGE_TOP,
            TRUE);

        gtk_layer_set_margin(
            gtk_win,
            GTK_LAYER_SHELL_EDGE_TOP,
            layout.offset);

        gtk_layer_set_margin(
            gtk_win,
            GTK_LAYER_SHELL_EDGE_LEFT,
            layout.distance);

        break;

    case DockLocation::right:

        gtk_layer_set_anchor(
            gtk_win,
            GTK_LAYER_SHELL_EDGE_RIGHT,
            TRUE);

        gtk_layer_set_anchor(
            gtk_win,
            GTK_LAYER_SHELL_EDGE_TOP,
            TRUE);

        gtk_layer_set_margin(
            gtk_win,
            GTK_LAYER_SHELL_EDGE_TOP,
            layout.offset);

        gtk_layer_set_margin(
            gtk_win,
            GTK_LAYER_SHELL_EDGE_RIGHT,
            layout.distance);

        break;
    }

    show_all();
}

// void DockTooltip::hide_tooltip()
// {
//     hide();
// }