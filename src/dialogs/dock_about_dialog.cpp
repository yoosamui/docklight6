// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// dock_about_dialog.cpp
//
// Implementation overview:
// Implements creation and presentation of the Docklight About dialog.
//
// Important implementation decisions:
// - The dialog is transient for the dock window.
// - Layer-shell integration keeps it above the dock where supported.
// - Application metadata and artwork are assembled at presentation time.
//
// ------------------------------------------------------------

#include "dock_about_dialog.h"
#include "config.h"

#include <glibmm/i18n.h>
#include <gtk-layer-shell.h>
#include <gtkmm.h>

namespace
{
void keep_dialog_above(
    Gtk::Window &dialog,
    Gtk::Window &parent,
    const char *name_space)
{
    dialog.set_keep_above(true);

    if (!gtk_layer_is_supported())
        return;

    auto *window = GTK_WINDOW(dialog.gobj());
    gtk_layer_init_for_window(window);
    gtk_layer_set_namespace(window, name_space);
    gtk_layer_set_layer(
        window,
        GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_exclusive_zone(window, 0);
    gtk_layer_set_keyboard_mode(
        window,
        GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

    const auto parent_window = parent.get_window();
    if (!parent_window)
        return;

    auto *display =
        gdk_window_get_display(parent_window->gobj());
    auto *monitor =
        gdk_display_get_monitor_at_window(
            display,
            parent_window->gobj());
    gtk_layer_set_monitor(window, monitor);
}
}

void DockAboutDialog::show(
    Gtk::Window &parent,
    const Glib::RefPtr<Gdk::Pixbuf> &icon)
{
    Gtk::Dialog dialog(
        _("About DockLight"),
        parent,
        true);

    dialog.set_type_hint(
        Gdk::WINDOW_TYPE_HINT_DIALOG);
    keep_dialog_above(
        dialog,
        parent,
        "docklight6-about");
    dialog.set_decorated(true);
    dialog.set_resizable(false);
    dialog.property_destroy_with_parent() =
        true;
    dialog.set_skip_taskbar_hint(true);
    dialog.set_skip_pager_hint(true);
    dialog.set_position(
        Gtk::WIN_POS_CENTER_ON_PARENT);
    dialog.set_default_size(
        600,
        -1);
    dialog.set_size_request(
        600,
        -1);

    Gtk::HeaderBar header;
    Gtk::Image header_icon;

    header.set_title(
        _("About DockLight"));
    header.set_show_close_button(true);
    header.set_decoration_layout(
        ":close");

    if (icon)
    {
        dialog.set_icon(icon);

        const auto small_home_icon =
            icon->scale_simple(
                20,
                20,
                Gdk::INTERP_BILINEAR);

        if (small_home_icon)
        {
            header_icon.set(
                small_home_icon);
            header.pack_start(
                header_icon);
        }
    }

    dialog.set_titlebar(header);

    dialog.add_button(
        _("_Close"),
        Gtk::RESPONSE_CLOSE);

    Gtk::Box about_content(
        Gtk::ORIENTATION_VERTICAL,
        10);
    Gtk::Image logo;
    Gtk::Label program_name;
    Gtk::Label version(
        Glib::ustring::compose(
            _("Version %1"),
            VERSION));
    Gtk::Label comments(
        _("A lightweight application dock.\n"
          "Author and Maintainer: yoosamui"));
    Gtk::LinkButton website(
        "https://github.com/yoosamui/DockLight",
        "yoosamui/DockLight");

    about_content.set_border_width(20);

    if (icon)
    {
        const auto logo_pixbuf =
            icon->scale_simple(
                96,
                96,
                Gdk::INTERP_BILINEAR);

        if (logo_pixbuf)
            logo.set(logo_pixbuf);
    }

    program_name.set_markup(
        "<span size=\"xx-large\" "
        "weight=\"bold\">Docklight 6.0</span>");
    program_name.set_justify(
        Gtk::JUSTIFY_CENTER);
    version.set_justify(
        Gtk::JUSTIFY_CENTER);
    comments.set_justify(
        Gtk::JUSTIFY_CENTER);
    website.set_halign(
        Gtk::ALIGN_CENTER);

    about_content.pack_start(
        logo,
        false,
        false);
    about_content.pack_start(
        program_name,
        false,
        false);
    about_content.pack_start(
        version,
        false,
        false);
    about_content.pack_start(
        comments,
        false,
        false);
    about_content.pack_start(
        website,
        false,
        false);

    dialog.get_content_area()
        ->pack_start(
            about_content,
            true,
            true);

    dialog.show_all_children();
    dialog.present();
    dialog.run();
    dialog.hide();
}
