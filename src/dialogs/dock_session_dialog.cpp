// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_session_dialog.cpp
//
// Implementation overview:
// Implements the UI-only Session editor dialog.
//
// Important implementation decisions:
// - Session Items are separate widgets so their behavior can evolve without
//   coupling Sessions to the dock window or menu.
// - Header and item values are transient widget state only.
// - Icon selection reuses GTK's icon theme and image-file chooser facilities.
//
// ------------------------------------------------------------

#include "dock_session_dialog.h"
#include "dock_session_item.h"
#include "integrations/desktop_session_identity.h"
#include "windowing/window_registry.h"

#include <gdk/gdkwayland.h>
#include <glibmm/i18n.h>
#include <gtkmm.h>

#include <optional>

namespace
{
void set_theme_icon(
    Gtk::Image &preview,
    const Glib::ustring &icon_name)
{
    if (icon_name.empty() || icon_name == "custom")
        return;

    preview.set_from_icon_name(
        icon_name,
        Gtk::ICON_SIZE_LARGE_TOOLBAR);
    preview.set_pixel_size(28);
}
}

void DockSessionDialog::show(
    Gtk::Window &parent,
    WindowRegistry *window_registry)
{
    Gtk::Dialog dialog(
        _("Session"),
        parent,
        true);

    dialog.set_type_hint(
        Gdk::WINDOW_TYPE_HINT_DIALOG);

    // A modal transient attached to the immovable dock cannot be moved
    // normally on GNOME Wayland. Match the existing dialog policy while
    // retaining application modality.
    auto *display = gdk_display_get_default();
    if ((display &&
         GDK_IS_WAYLAND_DISPLAY(display)) ||
        DesktopSessionIdentity::
            is_gnome_wayland_session())
    {
        dialog.unset_transient_for();
    }

    dialog.set_keep_above(true);
    dialog.set_decorated(true);
    dialog.set_resizable(true);
    dialog.property_destroy_with_parent() =
        true;
    dialog.set_skip_taskbar_hint(true);
    dialog.set_skip_pager_hint(true);
    dialog.set_position(Gtk::WIN_POS_CENTER);
    dialog.set_default_size(
        880,
        560);

    Gtk::HeaderBar titlebar;
    titlebar.set_title(
        _("Session"));
    titlebar.set_show_close_button(true);
    titlebar.set_decoration_layout(
        ":close");
    dialog.set_titlebar(titlebar);

    Gtk::Box editor(
        Gtk::ORIENTATION_VERTICAL,
        12);
    editor.set_border_width(14);
    editor.set_hexpand(true);
    editor.set_vexpand(true);

    Gtk::Grid header;
    header.set_column_spacing(8);
    header.set_row_spacing(8);
    header.set_hexpand(true);

    Gtk::Label session_name_label(
        _("_Session Name"),
        true);
    Gtk::Entry session_name;
    session_name.set_hexpand(true);
    session_name.set_placeholder_text(
        _("Session name"));
    session_name_label.set_mnemonic_widget(
        session_name);

    Gtk::Label icon_label(
        _("_Icon"),
        true);
    Gtk::Image icon_preview;
    Gtk::ComboBoxText icon_selector;
    icon_selector.append(
        "application-x-executable",
        _("Default"));
    icon_selector.append(
        "applications-internet",
        _("Internet"));
    icon_selector.append(
        "applications-office",
        _("Office"));
    icon_selector.append(
        "applications-development",
        _("Development"));
    icon_selector.append(
        "folder",
        _("Folder"));
    icon_selector.append(
        "custom",
        _("Custom"));
    icon_selector.set_active(0);
    icon_label.set_mnemonic_widget(
        icon_selector);
    set_theme_icon(
        icon_preview,
        icon_selector.get_active_id());

    Gtk::Button select_icon(
        _("Select _Icon"),
        true);
    Gtk::Button add_item(
        _("_Add"),
        true);

    header.attach(session_name_label, 0, 0, 1, 1);
    header.attach(session_name, 1, 0, 1, 1);
    header.attach(icon_label, 2, 0, 1, 1);
    header.attach(icon_preview, 3, 0, 1, 1);
    header.attach(icon_selector, 4, 0, 1, 1);
    header.attach(select_icon, 5, 0, 1, 1);
    header.attach(add_item, 6, 0, 1, 1);

    Gtk::ScrolledWindow item_scroller;
    item_scroller.set_policy(
        Gtk::POLICY_NEVER,
        Gtk::POLICY_AUTOMATIC);
    item_scroller.set_shadow_type(
        Gtk::SHADOW_IN);
    item_scroller.set_hexpand(true);
    item_scroller.set_vexpand(true);
    item_scroller.set_min_content_height(300);

    Gtk::Box item_list(
        Gtk::ORIENTATION_VERTICAL,
        10);
    item_list.set_border_width(10);
    item_list.set_hexpand(true);
    item_list.set_vexpand(false);
    item_scroller.add(item_list);

    editor.pack_start(header, false, false);
    editor.pack_start(item_scroller, true, true);
    dialog.get_content_area()->pack_start(
        editor,
        true,
        true);

    Glib::RefPtr<Gdk::Pixbuf> custom_icon;
    std::optional<WindowId> capture_window_id;
    sigc::connection capture_window_changed;

    const auto remember_active_window =
        [&capture_window_id,
         window_registry]()
    {
        if (window_registry &&
            window_registry->active_window())
        {
            capture_window_id =
                *window_registry->active_window();
        }
    };

    remember_active_window();
    if (window_registry)
    {
        capture_window_changed =
            window_registry->signal_changed().connect(
                remember_active_window);
    }

    const auto capture_window =
        [&capture_window_id,
         window_registry]()
        -> std::optional<ManagedWindow>
    {
        if (!window_registry ||
            !capture_window_id)
        {
            return std::nullopt;
        }

        const auto *window =
            window_registry->find_window(
                *capture_window_id);
        return window
                   ? std::optional<ManagedWindow>{*window}
                   : std::nullopt;
    };

    icon_selector.signal_changed().connect(
        [&icon_selector,
         &icon_preview,
         &custom_icon]()
        {
            const auto selected =
                icon_selector.get_active_id();

            if (selected == "custom")
            {
                if (custom_icon)
                    icon_preview.set(custom_icon);
                return;
            }

            set_theme_icon(
                icon_preview,
                selected);
        });

    select_icon.signal_clicked().connect(
        [&dialog,
         &icon_selector,
         &icon_preview,
         &custom_icon]()
        {
            Gtk::Dialog icon_dialog(
                _("Select Session Icon"),
                dialog,
                true);
            icon_dialog.set_resizable(true);
            icon_dialog.set_default_size(720, 480);
            icon_dialog.add_button(
                _("_Cancel"),
                Gtk::RESPONSE_CANCEL);
            icon_dialog.add_button(
                _("_Select"),
                Gtk::RESPONSE_OK);

            Gtk::FileChooserWidget chooser(
                Gtk::FILE_CHOOSER_ACTION_OPEN);
            auto image_filter =
                Gtk::FileFilter::create();
            image_filter->set_name(
                _("Image Files"));
            image_filter->add_pixbuf_formats();
            chooser.add_filter(image_filter);

            icon_dialog.get_content_area()->pack_start(
                chooser,
                true,
                true);
            icon_dialog.show_all_children();
            icon_dialog.present();

            if (icon_dialog.run() ==
                Gtk::RESPONSE_OK)
            {
                const auto filename =
                    chooser.get_filename();

                if (!filename.empty())
                {
                    try
                    {
                        custom_icon =
                            Gdk::Pixbuf::create_from_file(
                                filename,
                                28,
                                28,
                                true);
                        icon_preview.set(custom_icon);
                        icon_selector.set_active_id(
                            "custom");
                    }
                    catch (const Glib::Error &)
                    {
                        // The chooser filter limits selections to supported
                        // images. Leave the current preview intact if loading
                        // nevertheless fails.
                    }
                }
            }

            icon_dialog.hide();
        });

    add_item.signal_clicked().connect(
        [&item_list,
         &capture_window]()
        {
            auto *item =
                Gtk::manage(
                    new DockSessionItem(
                        capture_window));

            item->signal_remove_requested().connect(
                [item]()
                {
                    // Hiding removes the card from layout immediately while
                    // leaving GTK's managed lifetime with the dialog.
                    item->set_no_show_all(true);
                    item->hide();
                });

            item_list.pack_start(
                *item,
                false,
                false);
            item->show_all();
        });

    dialog.add_button(
        _("_Close"),
        Gtk::RESPONSE_CLOSE);

    dialog.show_all_children();
    dialog.present();
    dialog.run();
    dialog.hide();
    capture_window_changed.disconnect();
}
