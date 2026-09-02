// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_session_item.cpp
//
// Implementation overview:
// Builds one editable Session Item card and captures normalized window data.
//
// Important implementation decisions:
// - Paste copies available normalized metadata from the selected window.
// - Launch remains intentionally inert in this implementation step.
// - Remove emits a presentation signal; the containing editor decides which
//   card to remove.
// - Every value lives only in GTK widgets and is discarded with the dialog.
//
// ------------------------------------------------------------

#include "dock_session_item.h"

#include <gdkmm/pixbufloader.h>
#include <giomm/desktopappinfo.h>
#include <glibmm/i18n.h>

#include <utility>

namespace
{
Glib::RefPtr<Gio::DesktopAppInfo> find_desktop_application(
    const std::string &desktop_file_name)
{
    if (desktop_file_name.empty())
        return {};

    try
    {
        return Gio::DesktopAppInfo::create(
            desktop_file_name);
    }
    catch (const Glib::Error &)
    {
        return {};
    }
}

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

DockSessionItem::DockSessionItem(
    CaptureWindowProvider capture_window)
    : m_app_title_label(_("App Title")),
      m_app_title(true),
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
      m_capture_window(std::move(capture_window))
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

    m_app_title.set_hexpand(true);
    m_app_title.get_entry()->set_placeholder_text(
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
    m_parameters.set_placeholder_text(
        _("Command-line parameters"));
    m_workspace.set_placeholder_text(
        _("Workspace number"));
    m_dimensions.set_text("400x500");
    m_position.set_text("120x200");

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

    add(m_layout);

    m_paste_button.signal_clicked().connect(
        sigc::mem_fun(
            *this,
            &DockSessionItem::capture));

    m_remove_button.signal_clicked().connect(
        [this]()
        {
            m_remove_requested.emit();
        });
}

void DockSessionItem::capture()
{
    if (!m_capture_window)
        return;

    const auto captured = m_capture_window();
    if (!captured)
        return;

    const auto &window = *captured;
    const auto application =
        find_desktop_application(
            window.desktop_file_name);

    m_app_title.remove_all();
    if (!window.caption.empty())
    {
        m_app_title.append(window.caption);
        m_app_title.set_active(0);
    }
    else
    {
        m_app_title.get_entry()->set_text("");
    }

    m_desktop_file.set_text(
        application &&
                !application->get_filename().empty()
            ? application->get_filename()
            : window.desktop_file_name);
    m_app_name.set_text(
        application
            ? application->get_display_name()
            : std::string{});
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

sigc::signal<void> &DockSessionItem::signal_remove_requested()
{
    return m_remove_requested;
}
