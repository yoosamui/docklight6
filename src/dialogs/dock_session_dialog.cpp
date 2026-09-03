// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_session_dialog.cpp
//
// Implementation overview:
// Implements the Session editor dialog and its save/restore actions.
//
// Important implementation decisions:
// - Session Items are separate widgets so their behavior can evolve without
//   coupling Sessions to the dock window or menu.
// - Persistence goes through the borrowed LauncherManager, which owns
//   docklight.data. The dialog never opens that file itself.
// - Save keeps the dialog open so several Sessions can be stored in one visit.
// - Adding and restoring share one card factory, and removed cards are skipped
//   when saving because Remove hides rather than destroys them.
// - The icon selector is built from the installed images directory rather than
//   a hard-coded list, so adding an icon is a pure data change.
// - Icon selection is owned by the image-backed combobox and file chooser.
//
// ------------------------------------------------------------

#include "dock_session_dialog.h"
#include "dock_session_editor_item.h"
#include "integrations/desktop_session_identity.h"
#include "launchers/launcher_manager.h"
#include "windowing/window_registry.h"

#include <gdk/gdkwayland.h>
#include <glibmm/fileutils.h>
#include <glibmm/i18n.h>
#include <glibmm/miscutils.h>
#include <gtkmm.h>

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace
{
constexpr int ICON_ROW_SIZE = 24;

std::string trimmed_session_name(
    const Glib::ustring &value)
{
    const auto text = value.raw();
    const auto first = text.find_first_not_of(
        " \t\n\r\f\v");

    if (first == std::string::npos)
        return {};

    const auto last = text.find_last_not_of(
        " \t\n\r\f\v");
    return text.substr(
        first,
        last - first + 1);
}

struct SessionIcon
{
    std::string path;
    Glib::ustring label;
};

// Gtk::ComboBoxText renders text only. The selector shows each icon beside its
// name, so it needs an explicit model with a pixbuf column.
class SessionIconColumns
    : public Gtk::TreeModel::ColumnRecord
{
  public:
    SessionIconColumns()
    {
        add(id);
        add(label);
        add(image);
    }

    Gtk::TreeModelColumn<Glib::ustring> id;
    Gtk::TreeModelColumn<Glib::ustring> label;
    Gtk::TreeModelColumn<
        Glib::RefPtr<Gdk::Pixbuf>>
        image;
};

// The installed location wins, so a system installation is not shadowed by a
// stale checkout. The source tree is the fallback for an uninstalled run, the
// same ordering DockHomeItem uses for its own icon.
std::vector<std::string> session_image_directories()
{
    return {
        Glib::build_filename(
            DOCKLIGHT_DATA_DIR,
            "images"),
        Glib::build_filename(
            SOURCE_DIR,
            "..",
            "data",
            "images")};
}

bool is_supported_image(
    const std::string &name)
{
    const auto separator =
        name.find_last_of('.');
    if (separator == std::string::npos)
        return false;

    auto extension =
        name.substr(separator + 1);
    std::transform(
        extension.begin(),
        extension.end(),
        extension.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(
                std::tolower(character));
        });

    static const std::set<std::string>
        supported = {
            "png",
            "svg",
            "jpg",
            "jpeg",
            "bmp",
            "xpm"};

    return supported.count(extension) != 0;
}

// The first directory that holds any image wins outright. Merging the
// directories would show an installed set and a checkout set at the same time,
// which is how a stale installed image can survive its own deletion.
std::vector<SessionIcon> available_session_icons()
{
    for (const auto &directory :
         session_image_directories())
    {
        std::vector<SessionIcon> result;

        try
        {
            Glib::Dir images(directory);

            for (const auto &name : images)
            {
                if (!is_supported_image(name))
                    continue;

                const auto path =
                    Glib::build_filename(
                        directory,
                        name);
                if (!Glib::file_test(
                        path,
                        Glib::FILE_TEST_IS_REGULAR))
                {
                    continue;
                }

                result.push_back(
                    SessionIcon{
                        path,
                        Glib::path_get_basename(
                            name.substr(
                                0,
                                name.find_last_of(
                                    '.')))});
            }
        }
        catch (const Glib::FileError &)
        {
            // A missing images directory simply contributes no icons.
            continue;
        }

        if (result.empty())
            continue;

        std::sort(
            result.begin(),
            result.end(),
            [](const SessionIcon &first,
               const SessionIcon &second)
            {
                return first.label < second.label;
            });

        return result;
    }

    return {};
}

Glib::RefPtr<Gdk::Pixbuf> load_image_icon(
    const std::string &path,
    int size)
{
    try
    {
        return Gdk::Pixbuf::create_from_file(
            path,
            size,
            size,
            true);
    }
    catch (const Glib::Error &)
    {
        return {};
    }
}

}

void DockSessionDialog::show(
    Gtk::Window &parent,
    const Glib::RefPtr<Gdk::Pixbuf> &icon,
    WindowRegistry *window_registry,
    LauncherManager &launcher_manager,
    const std::function<void(const SessionRecord &)>
        &on_sessions_changed,
    const std::string &initial_session_name)
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
    Gtk::Image header_icon;
    titlebar.set_title(
        _("Session"));
    titlebar.set_show_close_button(true);
    titlebar.set_decoration_layout(
        ":close");
    dialog.set_titlebar(titlebar);

    if (icon)
    {
        dialog.set_icon(icon);

        const auto small_icon =
            icon->scale_simple(
                20,
                20,
                Gdk::INTERP_BILINEAR);
        if (small_icon)
        {
            header_icon.set(small_icon);
            titlebar.pack_start(header_icon);
        }
    }

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
    Gtk::ComboBoxText session_name(true);
    session_name.set_hexpand(true);
    session_name.get_entry()->set_placeholder_text(
        _("Session name"));
    session_name_label.set_mnemonic_widget(
        session_name);

    Gtk::Label icon_label(
        _("_Icon"),
        true);

    SessionIconColumns icon_columns;
    auto icon_model =
        Gtk::ListStore::create(icon_columns);

    // Assign the model separately: the RefPtr overload of the constructor is
    // ambiguous against the has-entry overload.
    Gtk::ComboBox icon_selector;
    icon_selector.set_model(icon_model);
    icon_selector.pack_start(
        icon_columns.image,
        false);
    icon_selector.pack_start(
        icon_columns.label,
        true);
    icon_selector.set_id_column(
        icon_columns.id.index());

    // The list holds the images from the images directory and nothing else.
    // Each row carries its own file name and its own rendering, and the first
    // row is the default selection.
    const auto session_icons =
        available_session_icons();
    for (const auto &icon : session_icons)
    {
        auto row = *icon_model->append();
        row[icon_columns.id] = icon.path;
        row[icon_columns.label] = icon.label;
        row[icon_columns.image] =
            load_image_icon(
                icon.path,
                ICON_ROW_SIZE);
    }

    icon_label.set_mnemonic_widget(
        icon_selector);

    if (!session_icons.empty())
        icon_selector.set_active(0);

    Gtk::Button select_icon(
        _("Select _Icon"),
        true);
    Gtk::Button add_item(
        _("_Add"),
        true);

    // Adding an item needs both a session name and a selected icon, so the
    // action stays insensitive until the header is complete.
    const auto update_add_sensitivity =
        [&session_name,
         &icon_selector,
         &add_item]()
    {
        const auto name =
            session_name.get_entry()->get_text();
        const bool has_name =
            name.find_first_not_of(
                " \t\n\r\f\v") !=
            Glib::ustring::npos;
        const bool has_icon =
            !icon_selector.get_active_id()
                 .empty();

        add_item.set_sensitive(
            has_name && has_icon);
    };

    session_name.get_entry()
        ->signal_changed()
        .connect(update_add_sensitivity);
    icon_selector.signal_changed().connect(
        update_add_sensitivity);
    update_add_sensitivity();

    header.attach(session_name_label, 0, 0, 1, 1);
    header.attach(session_name, 1, 0, 1, 1);
    header.attach(icon_label, 2, 0, 1, 1);
    header.attach(icon_selector, 3, 0, 1, 1);
    header.attach(select_icon, 4, 0, 1, 1);
    header.attach(add_item, 5, 0, 1, 1);

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

    // The Custom row exists only once Select Icon has produced an image the
    // installed set does not contain. Its identifier is that file's path, so
    // selecting it needs no special case.
    Gtk::TreeModel::iterator custom_row;

    const auto row_with_id =
        [&icon_model,
         &icon_columns](
            const Glib::ustring &id)
    {
        auto row = icon_model->children().begin();
        for (; row != icon_model->children().end();
             ++row)
        {
            if (Glib::ustring(
                    (*row)[icon_columns.id]) == id)
            {
                break;
            }
        }
        return row;
    };

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

    select_icon.signal_clicked().connect(
        [&dialog,
         &icon_selector,
         &icon_model,
         &icon_columns,
         &custom_row,
         &row_with_id]()
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
                        const auto custom_icon =
                            load_image_icon(
                                filename,
                                ICON_ROW_SIZE);
                        if (!custom_icon)
                        {
                            icon_dialog.hide();
                            return;
                        }

                        const auto listed =
                            row_with_id(filename);

                        if (listed !=
                            icon_model->children()
                                .end())
                        {
                            // The chosen file is already one of the installed
                            // images. Select it under its own name instead of
                            // duplicating it as Custom.
                            icon_selector.set_active(
                                listed);
                        }
                        else
                        {
                            if (!custom_row)
                            {
                                custom_row =
                                    icon_model
                                        ->append();
                            }

                            (*custom_row)
                                [icon_columns.id] =
                                    filename;
                            (*custom_row)
                                [icon_columns.label] =
                                    _("Custom");
                            (*custom_row)
                                [icon_columns.image] =
                                    custom_icon;

                            icon_selector.set_active(
                                custom_row);
                        }
                    }
                    catch (const Glib::Error &)
                    {
                        // The chooser filter limits selections to supported
                        // images. Leave the current selection unchanged if
                        // loading nevertheless fails.
                    }
                }
            }

            icon_dialog.hide();
        });

    // Declared before the card factory because a card is created before
    // save_session exists, and assigned once it does.
    std::function<void()> schedule_save;

    // Adding and restoring build the same card, so the factory is shared
    // rather than living inside the Add handler.
    const auto create_item =
        [&item_list,
         &capture_window,
         &schedule_save,
         window_registry]()
    {
        auto *item =
            Gtk::manage(
                new DockSessionEditorItem(
                    capture_window,
                    window_registry));

        item->signal_remove_requested().connect(
            [item, &schedule_save]()
            {
                // Hiding removes the card from layout immediately while
                // leaving GTK's managed lifetime with the dialog.
                item->set_no_show_all(true);
                item->hide();

                if (schedule_save)
                    schedule_save();
            });

        item->signal_changed().connect(
            [&schedule_save]()
            {
                if (schedule_save)
                    schedule_save();
            });

        item_list.pack_start(
            *item,
            false,
            false);
        item->show_all();
        return item;
    };

    // The first Add fixes which Session is being edited. Locking the name
    // keeps every card in one Session and stops the combo from loading a
    // different one over the cards just added.
    const auto lock_session_name =
        [&session_name]()
    {
        session_name.get_entry()
            ->set_editable(false);
        session_name.set_button_sensitivity(
            Gtk::SENSITIVITY_OFF);
    };

    // Removed cards stay alive but hidden, so saving walks the live ones only.
    const auto live_items =
        [&item_list]()
    {
        std::vector<DockSessionEditorItem *> items;

        for (auto *child :
             item_list.get_children())
        {
            auto *item =
                dynamic_cast<DockSessionEditorItem *>(
                    child);

            if (item && !item->removed())
                items.push_back(item);
        }

        return items;
    };

    // Empty means a new Session. Loading an existing row stores its original
    // key here, so changing the entry text can rename that same record.
    std::string editing_session_name;

    const auto save_session =
        [&dialog,
         &session_name,
         &icon_selector,
         &live_items,
         &launcher_manager,
         &editing_session_name,
         &on_sessions_changed]()
    {
        SessionRecord record;
        record.name =
            trimmed_session_name(
                session_name.get_entry()
                    ->get_text());
        record.icon =
            icon_selector.get_active_id();

        for (auto *item : live_items())
        {
            SessionItemRecord stored;
            stored.desktop_file =
                item->desktop_file();
            stored.title = item->app_title();
            stored.parameters =
                item->parameters();
            stored.workspace = item->workspace();
            stored.dimensions =
                item->dimensions();
            stored.position = item->position();

            if (stored.desktop_file.empty())
                continue;

            record.items.push_back(
                std::move(stored));
        }

        const auto names_before_save =
            launcher_manager.session_names();
        const bool name_changed =
            !editing_session_name.empty() &&
            record.name != editing_session_name;
        const bool name_conflict =
            record.name != editing_session_name &&
            std::find(
                names_before_save.begin(),
                names_before_save.end(),
                record.name) !=
                names_before_save.end();

        if (name_conflict)
        {
            Gtk::MessageDialog message(
                dialog,
                _("A Session with this name already exists."),
                false,
                Gtk::MESSAGE_ERROR,
                Gtk::BUTTONS_OK,
                true);
            message.set_secondary_text(
                Glib::ustring::compose(
                    _("Choose a different name for “%1”."),
                    record.name));
            message.run();
            return;
        }

        const bool saved =
            editing_session_name.empty()
                ? launcher_manager.save_session(
                      record)
                : launcher_manager.rename_session(
                      editing_session_name,
                      record);

        if (!saved)
        {
            g_warning(
                "Cannot save Session '%s'",
                record.name.c_str());
            return;
        }

        if (name_changed)
        {
            const auto old_row =
                std::find(
                    names_before_save.begin(),
                    names_before_save.end(),
                    editing_session_name);

            if (old_row != names_before_save.end())
            {
                const auto position =
                    static_cast<int>(
                        std::distance(
                            names_before_save.begin(),
                            old_row));
                session_name.remove_text(
                    position);
                session_name.insert(
                    position,
                    record.name);
                session_name.set_active(
                    position);
            }
        }

        editing_session_name = record.name;

        if (on_sessions_changed)
        {
            const auto saved_sessions = launcher_manager.sessions();
            const auto saved = std::find_if(
                saved_sessions.begin(), saved_sessions.end(),
                [&record](const SessionRecord &candidate)
                {
                    return candidate.name == record.name;
                });
            if (saved != saved_sessions.end())
                on_sessions_changed(*saved);
        }
    };

    // Add commits the Session immediately: the dock item is expected to appear
    // at once, and a dock item that is not backed by the store would vanish on
    // the next restart.
    add_item.signal_clicked().connect(
        [&create_item,
         &lock_session_name,
         &save_session]()
        {
            lock_session_name();
            create_item();
            save_session();
        });

    // Editing continues after Add, so changes are written back on a short
    // debounce. Without this the most recently added item would only reach the
    // store if the user pressed Save again before closing.
    sigc::connection save_debounce;

    schedule_save =
        [&save_debounce,
         &save_session]()
    {
        save_debounce.disconnect();
        save_debounce =
            Glib::signal_timeout().connect(
                [&save_session]()
                {
                    save_session();
                    return false;
                },
                400);
    };

    // Restoring replaces the card list and re-selects the stored icon in the
    // combobox, adding a Custom row when that image is not installed.
    const auto load_session =
        [&item_list,
         &icon_selector,
         &icon_model,
         &icon_columns,
         &custom_row,
         &row_with_id,
         &create_item,
         &launcher_manager,
         &editing_session_name](
            const std::string &name)
    {
        const auto sessions =
            launcher_manager.sessions();
        const auto stored_session =
            std::find_if(
                sessions.begin(),
                sessions.end(),
                [&name](
                    const SessionRecord &session)
                {
                    return session.name == name;
                });

        // An editable combo can emit changed while its entry and active row
        // are between states. Never erase the currently loaded cards unless
        // the selected name has first resolved to a stored Session.
        if (stored_session == sessions.end())
            return false;

        for (auto *child :
             item_list.get_children())
        {
            item_list.remove(*child);
        }

        const auto &session = *stored_session;
        editing_session_name =
            session.name;

        if (!session.icon.empty())
        {
            const auto listed =
                row_with_id(session.icon);

            if (listed !=
                icon_model->children().end())
            {
                icon_selector.set_active(
                    listed);
            }
            else
            {
                if (!custom_row)
                {
                    custom_row =
                        icon_model->append();
                }

                (*custom_row)
                    [icon_columns.id] =
                        session.icon;
                (*custom_row)
                    [icon_columns.label] =
                        _("Custom");
                (*custom_row)
                    [icon_columns.image] =
                        load_image_icon(
                            session.icon,
                            ICON_ROW_SIZE);
                icon_selector.set_active(
                    custom_row);
            }

        }

        for (const auto &item :
             session.items)
        {
            create_item()->restore(
                item.desktop_file,
                item.title,
                item.parameters,
                item.workspace,
                item.dimensions,
                item.position);
        }

        return true;
    };

    // Selecting a stored name loads it. Typing a new one leaves the current
    // cards alone so a session can be saved under a different name.
    session_name.signal_changed().connect(
        [&session_name,
         &load_session]()
        {
            const auto selected =
                session_name.get_active_row_number();

            if (selected < 0)
                return;

            load_session(
                session_name.get_active_text());
        });

    int initial_session_index = -1;
    int session_index = 0;
    for (const auto &name :
         launcher_manager.session_names())
    {
        session_name.append(name);
        if (name == initial_session_name)
            initial_session_index = session_index;
        ++session_index;
    }

    if (initial_session_index >= 0)
    {
        session_name.set_active(
            initial_session_index);

        // Gtk normally emits changed synchronously, but the explicit call
        // guarantees the Edit action cannot open with an empty card list if
        // the editable entry has not caught up with its active row yet.
        if (editing_session_name !=
            initial_session_name)
        {
            load_session(
                initial_session_name);
        }
    }

    auto *save_button =
        dialog.add_button(
            _("_Save"),
            Gtk::RESPONSE_APPLY);
    save_button->signal_clicked().connect(
        save_session);

    dialog.add_button(
        _("_Cancel"),
        Gtk::RESPONSE_CANCEL);

    dialog.show_all_children();
    dialog.present();

    // Save already ran from the button's own click handler. Re-entering the
    // loop keeps the editor open so several Sessions can be saved in a row;
    // only Close and the window manager end it.
    while (dialog.run() == Gtk::RESPONSE_APPLY)
    {
    }

    dialog.hide();

    // A pending debounce would run after these locals are gone. Flush it
    // instead, so a change made just before closing is not lost.
    if (save_debounce.connected())
    {
        save_debounce.disconnect();
        save_session();
    }

    capture_window_changed.disconnect();
}
