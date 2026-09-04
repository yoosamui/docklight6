// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_session_editor_item.cpp
//
// Implementation overview:
// Builds one editable Session Item card, captures normalized window data, and
// launches one application instance with deferred window placement.
//
// Important implementation decisions:
// - Paste copies available normalized metadata from the selected window.
// - Desktop entries resolve by path, desktop ID, and finally by a normalized
//   scan of installed applications so window identities that are only a
//   WM_CLASS or an executable name remain launchable.
// - Launch preserves the entry's own Exec arguments and appends Parameters,
//   because an executable name alone loses wrappers such as flatpak or env.
// - Failures report into the card instead of only warning on stderr.
// - Placement waits for the matching new/activated window and then crosses the
//   WindowRegistry boundary exactly once.
// - Remove emits a presentation signal; the containing editor decides which
//   card to remove.
// - Every value lives only in GTK widgets and is discarded with the dialog.
//
// ------------------------------------------------------------

#include "dock_session_editor_item.h"

#include <gdkmm/pixbufloader.h>
#include <giomm/desktopappinfo.h>
#include <glibmm/i18n.h>

#include <string>
#include <utility>
#include <vector>

namespace
{
Glib::RefPtr<Gdk::Pixbuf> load_window_icon(
    const std::vector<unsigned char> &icon_png)
{
    if (icon_png.empty())
        return {};

    try
    {
        auto loader = Gdk::PixbufLoader::create();
        loader->write(
            icon_png.data(),
            icon_png.size());
        loader->close();

        const auto pixbuf = loader->get_pixbuf();
        return pixbuf
                   ? pixbuf->scale_simple(
                         40,
                         40,
                         Gdk::INTERP_BILINEAR)
                   : Glib::RefPtr<Gdk::Pixbuf>{};
    }
    catch (const Glib::Error &)
    {
        return {};
    }
}

std::string workspace_text(
    const ManagedWindow &window)
{
    std::string result;

    const auto append =
        [&result](const std::string &value)
    {
        if (!result.empty())
            result += ", ";
        result += value;
    };

    for (const auto number :
         window.desktop_numbers)
    {
        append(std::to_string(number));
    }

    if (!result.empty())
        return result;

    for (const auto &id :
         window.desktop_ids)
    {
        append(id);
    }

    return result;
}
}

DockSessionEditorItem::DockSessionEditorItem(
    CaptureWindowProvider capture_window,
    WindowRegistry *window_registry)
    : m_app_title_label(_("App Title")),
      m_actions(Gtk::ORIENTATION_HORIZONTAL, 6),
      m_paste_button(_("_Paste"), true),
      m_launch_button(_("_Launch"), true),
      m_remove_button(_("_Remove"), true),
      m_desktop_file_label(_("Desktop File")),
      m_app_name_label(_("App Name")),
      m_parameters_label(_("Parameters")),
      m_workspace_label(_("Workspace")),
      m_dimensions_label(_("Dimensions")),
      m_position_label(_("Position")),
      m_capture_window(std::move(capture_window)),
      m_launcher(window_registry)
{
    set_shadow_type(Gtk::SHADOW_ETCHED_IN);
    set_hexpand(true);

    m_layout.set_border_width(12);
    m_layout.set_row_spacing(8);
    m_layout.set_column_spacing(10);
    m_layout.set_hexpand(true);

    m_app_icon.set_from_icon_name(
        "application-x-executable",
        Gtk::ICON_SIZE_DIALOG);
    m_app_icon.set_pixel_size(40);
    m_app_icon.set_valign(Gtk::ALIGN_START);

    m_app_title_label.set_halign(Gtk::ALIGN_START);
    m_desktop_file_label.set_halign(Gtk::ALIGN_START);
    m_app_name_label.set_halign(Gtk::ALIGN_START);
    m_parameters_label.set_halign(Gtk::ALIGN_START);
    m_workspace_label.set_halign(Gtk::ALIGN_START);
    m_dimensions_label.set_halign(Gtk::ALIGN_START);
    m_position_label.set_halign(Gtk::ALIGN_START);

    m_app_title.set_editable(false);
    m_app_title.set_max_length(40);
    m_app_title.set_hexpand(true);
    m_app_title.set_placeholder_text(
        _("Application title"));
    m_desktop_file.set_editable(false);
    m_desktop_file.set_hexpand(true);
    m_desktop_file.set_placeholder_text(
        _("Desktop file"));
    m_app_name.set_editable(false);
    m_app_name.set_hexpand(true);
    m_app_name.set_placeholder_text(
        _("Application name"));
    m_parameters.set_hexpand(true);
    m_parameters.set_max_length(512);
    m_parameters.set_placeholder_text(
        _("Command-line parameters"));
    m_workspace.set_max_length(2);
    m_workspace.set_input_purpose(
        Gtk::INPUT_PURPOSE_DIGITS);
    m_workspace.set_placeholder_text(
        _("Workspace number"));
    m_dimensions.set_max_length(32);
    m_dimensions.set_text("400x500");
    m_position.set_max_length(32);
    m_position.set_text("120x200");

    // Launch failures are ordinary user-facing conditions, not diagnostics.
    // Keep them on the card instead of only in the process log.
    m_status.set_halign(Gtk::ALIGN_START);
    m_status.set_xalign(0.0F);
    m_status.set_line_wrap(true);
    m_status.set_no_show_all(true);
    m_status.get_style_context()->add_class(
        GTK_STYLE_CLASS_DIM_LABEL);

    m_actions.pack_start(
        m_paste_button,
        false,
        false);
    m_actions.pack_start(
        m_launch_button,
        false,
        false);
    m_actions.pack_start(
        m_remove_button,
        false,
        false);

    m_layout.attach(m_app_icon, 0, 0, 1, 3);
    m_layout.attach(m_app_title_label, 1, 0, 1, 1);
    m_layout.attach(m_app_title, 2, 0, 1, 1);
    m_layout.attach(m_actions, 3, 0, 1, 1);
    m_layout.attach(m_desktop_file_label, 1, 1, 1, 1);
    m_layout.attach(m_desktop_file, 2, 1, 2, 1);
    m_layout.attach(m_app_name_label, 1, 2, 1, 1);
    m_layout.attach(m_app_name, 2, 2, 2, 1);
    m_layout.attach(m_parameters_label, 1, 3, 1, 1);
    m_layout.attach(m_parameters, 2, 3, 2, 1);
    m_layout.attach(m_workspace_label, 1, 4, 1, 1);
    m_layout.attach(m_workspace, 2, 4, 2, 1);
    m_layout.attach(m_dimensions_label, 1, 5, 1, 1);
    m_layout.attach(m_dimensions, 2, 5, 2, 1);
    m_layout.attach(m_position_label, 1, 6, 1, 1);
    m_layout.attach(m_position, 2, 6, 2, 1);
    m_layout.attach(m_status, 1, 7, 3, 1);

    add(m_layout);

    m_paste_button.signal_clicked().connect(
        sigc::mem_fun(
            *this,
            &DockSessionEditorItem::capture));

    m_launch_button.signal_clicked().connect(
        sigc::mem_fun(
            *this,
            &DockSessionEditorItem::launcher));

    m_remove_button.signal_clicked().connect(
        [this]()
        {
            m_removed = true;
            m_remove_requested.emit();
        });

    for (auto *entry : {&m_parameters,
                        &m_workspace,
                        &m_dimensions,
                        &m_position})
    {
        entry->signal_changed().connect(
            [this]()
            {
                if (!m_restoring)
                    m_changed.emit();
            });
    }
}

DockSessionEditorItem::~DockSessionEditorItem() = default;

void DockSessionEditorItem::capture()
{
    report({});

    if (!m_capture_window)
        return;

    const auto captured = m_capture_window();
    if (!captured)
    {
        report(
            _("No application window is available to copy."));
        return;
    }

    const auto &window = *captured;
    const auto application =
        find_session_application(
            window.desktop_file_name);

    m_app_title.set_text(window.caption);

    apply_application(
        window.desktop_file_name);
    m_workspace.set_text(
        workspace_text(window));
    m_dimensions.set_text(
        std::to_string(
            window.frame_geometry.width) +
        "x" +
        std::to_string(
            window.frame_geometry.height));
    m_position.set_text(
        std::to_string(
            window.frame_geometry.x) +
        "x" +
        std::to_string(
            window.frame_geometry.y));

    if (!m_restoring)
        m_changed.emit();

    const auto window_icon =
        load_window_icon(window.icon_png);
    if (window_icon)
    {
        m_app_icon.set(window_icon);
    }
    else if (application && application->get_icon())
    {
        const Glib::RefPtr<const Gio::Icon>
            application_icon =
                application->get_icon();
        m_app_icon.set(
            application_icon,
            Gtk::ICON_SIZE_DIALOG);
        m_app_icon.set_pixel_size(40);
    }
    else if (!window.icon_name.empty())
    {
        m_app_icon.set_from_icon_name(
            window.icon_name,
            Gtk::ICON_SIZE_DIALOG);
        m_app_icon.set_pixel_size(40);
    }
}

// Fills the read-only Desktop File, App Name, and icon from one desktop
// identity. Shared by Paste and by restoring a stored item.
void DockSessionEditorItem::apply_application(
    const std::string &desktop_file)
{
    const auto application =
        find_session_application(desktop_file);

    m_desktop_file.set_text(
        application &&
                !application->get_filename().empty()
            ? application->get_filename()
            : desktop_file);
    m_app_name.set_text(
        application
            ? application->get_display_name()
            : std::string{});

    if (application && application->get_icon())
    {
        const Glib::RefPtr<const Gio::Icon>
            application_icon =
                application->get_icon();
        m_app_icon.set(
            application_icon,
            Gtk::ICON_SIZE_DIALOG);
        m_app_icon.set_pixel_size(40);
    }
}

void DockSessionEditorItem::restore(
    const std::string &desktop_file,
    const std::string &app_title,
    const std::string &parameters,
    const std::string &workspace,
    const std::string &dimensions,
    const std::string &position)
{
    report({});

    m_restoring = true;

    m_app_title.set_text(app_title);
    m_parameters.set_text(parameters);
    m_workspace.set_text(workspace);

    if (!dimensions.empty())
        m_dimensions.set_text(dimensions);
    if (!position.empty())
        m_position.set_text(position);

    apply_application(desktop_file);

    m_restoring = false;
}

std::string DockSessionEditorItem::desktop_file() const
{
    return m_desktop_file.get_text();
}

std::string DockSessionEditorItem::app_title() const
{
    return m_app_title.get_text();
}

std::string DockSessionEditorItem::app_name() const
{
    return m_app_name.get_text();
}

std::string DockSessionEditorItem::parameters() const
{
    return m_parameters.get_text();
}

std::string DockSessionEditorItem::workspace() const
{
    return m_workspace.get_text();
}

std::string DockSessionEditorItem::dimensions() const
{
    return m_dimensions.get_text();
}

std::string DockSessionEditorItem::position() const
{
    return m_position.get_text();
}

bool DockSessionEditorItem::removed() const
{
    return m_removed;
}

void DockSessionEditorItem::report(
    const Glib::ustring &message)
{
    m_status.set_text(message);
    m_status.set_visible(!message.empty());
}

// The card owns no launch machinery of its own. It packages its fields and
// hands them to the shared SessionLauncher, so a card and a dock Session item
// resolve, launch, and place identically.
void DockSessionEditorItem::launcher()
{
    report({});

    if (m_desktop_file.get_text().empty())
    {
        report(
            _("Paste a window into this item before launching it."));
        return;
    }

    SessionItemRecord item;
    item.desktop_file = desktop_file();
    item.title = app_title();
    item.parameters = parameters();
    item.workspace = workspace();
    item.dimensions = dimensions();
    item.position = position();

    const auto error = m_launcher.launch(item);

    if (!error.empty())
    {
        report(error);
        g_warning(
            "Cannot launch Session item '%s'",
            item.desktop_file.c_str());
        return;
    }

    if (!m_launcher.can_place())
    {
        // Placement crosses the window backend, so an unsupported or outdated
        // desktop integration cannot honor Workspace, Dimensions, or Position.
        report(
            _("The application was launched. This desktop session cannot "
              "place windows, so Workspace, Dimensions, and Position were "
              "ignored."));
    }
}


sigc::signal<void> &DockSessionEditorItem::signal_remove_requested()
{
    return m_remove_requested;
}

sigc::signal<void> &DockSessionEditorItem::signal_changed()
{
    return m_changed;
}
