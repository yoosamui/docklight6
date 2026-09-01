// ------------------------------------------------------------
// Docklight 6.0
//
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
#include "integrations/desktop_session_identity.h"
#include "presentation/docklight_surface_identity.h"

#include <glibmm/i18n.h>
#include <gdk/gdkwayland.h>
#include <gtk-layer-shell.h>
#include <gtkmm.h>

#include <algorithm>

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

void center_dialog_on_parent_monitor(
    Gtk::Window &dialog,
    Gtk::Window &parent)
{
    if (gtk_layer_is_supported())
        return;

    const auto parent_window = parent.get_window();
    if (!parent_window)
        return;

    const auto display = parent_window->get_display();
    const auto monitor = display
                             ? display->get_monitor_at_window(
                                   parent_window)
                             : Glib::RefPtr<Gdk::Monitor>{};
    if (!monitor)
        return;

    Gdk::Rectangle geometry;
    monitor->get_geometry(geometry);

    Gtk::Requisition minimum;
    Gtk::Requisition natural;
    dialog.get_preferred_size(minimum, natural);
    const int width = std::max(
        1,
        natural.width);
    const int height = std::max(
        1,
        natural.height);

    dialog.set_position(Gtk::WIN_POS_NONE);
    dialog.move(
        geometry.get_x() +
            (geometry.get_width() - width) / 2,
        geometry.get_y() +
            (geometry.get_height() - height) / 2);
}
}

void DockAboutDialog::show(
    Gtk::Window &parent,
    const Glib::RefPtr<Gdk::Pixbuf> &icon,
    const DockRuntimeInfo &runtime_info)
{
    Gtk::Dialog dialog(
        _("About DockLight"),
        parent,
        true);

    dialog.set_type_hint(
        Gdk::WINDOW_TYPE_HINT_DIALOG);

    // A modal transient is attached to the immovable edge dock by Mutter.
    // This applies to GNOME's default XWayland presentation as well as a
    // native Wayland surface and makes an interactive dialog move stall.
    // Keep About as an independent toplevel and centre it below.
    auto *display = gdk_display_get_default();
    if (((display &&
          GDK_IS_WAYLAND_DISPLAY(display)) ||
         DesktopSessionIdentity::
             is_gnome_wayland_session()) &&
        !gtk_layer_is_supported())
    {
        dialog.unset_transient_for();
    }

    gtk_window_set_role(
        GTK_WINDOW(dialog.gobj()),
        DocklightSurfaceIdentity::ABOUT_ROLE);

    keep_dialog_above(
        dialog,
        parent,
        DocklightSurfaceIdentity::ABOUT_NAMESPACE);
    dialog.set_decorated(true);
    dialog.set_resizable(false);
    dialog.property_destroy_with_parent() =
        true;
    dialog.set_skip_taskbar_hint(true);
    dialog.set_skip_pager_hint(true);
    dialog.set_position(Gtk::WIN_POS_NONE);
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
          "Author and Maintainer: yoosamui\n\n"
          "Copyright © 2018-2026 Juan González"));

    Gtk::LinkButton website(
        "https://github.com/yoosamui/DockLight",
        "yoosamui/DockLight");
    Gtk::Separator details_separator;
    Gtk::Label runtime_details(
        Glib::ustring::compose(
            _("Presentation mode: %1\n"
              "Dock configuration loaded: %2\n"
              "detected Desktop: %3\n"
              "detected WM: %4\n"
              "detected compositor: %5\n"
              "selected backend: %6"),
            runtime_info.presentation_mode,
            runtime_info.configuration_path,
            runtime_info.desktop,
            runtime_info.window_manager,
            runtime_info.compositor,
            runtime_info.backend));

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
    runtime_details.set_halign(
        Gtk::ALIGN_FILL);
    runtime_details.set_xalign(0.0f);
    runtime_details.set_justify(
        Gtk::JUSTIFY_LEFT);
    runtime_details.set_selectable(true);
    runtime_details.set_line_wrap(true);
    runtime_details.set_line_wrap_mode(
        Pango::WRAP_WORD_CHAR);
    runtime_details.set_max_width_chars(72);

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
    about_content.pack_start(
        details_separator,
        false,
        false);
    about_content.pack_start(
        runtime_details,
        false,
        false);

    dialog.get_content_area()
        ->pack_start(
            about_content,
            true,
            true);

    dialog.show_all_children();
    center_dialog_on_parent_monitor(
        dialog,
        parent);
    dialog.present();
    dialog.run();
    dialog.hide();
}
