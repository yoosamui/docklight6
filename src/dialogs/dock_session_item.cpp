// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_session_item.cpp
//
// Implementation overview:
// Builds one editable Session Item card, captures normalized window data, and
// launches one application instance with deferred window placement.
//
// Important implementation decisions:
// - Paste copies available normalized metadata from the selected window.
// - Launch uses a desktop entry when available and safely parses Parameters.
// - Placement waits for the matching new/activated window and then crosses the
//   WindowRegistry boundary exactly once.
// - Remove emits a presentation signal; the containing editor decides which
//   card to remove.
// - Every value lives only in GTK widgets and is discarded with the dialog.
//
// ------------------------------------------------------------

#include "dock_session_item.h"
#include "presentation/presentation_selector.h"
#include "windowing/window_registry.h"

#include <gdkmm/pixbufloader.h>
#include <giomm/desktopappinfo.h>
#include <glibmm/i18n.h>
#include <glibmm/miscutils.h>
#include <glibmm/shell.h>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <string_view>
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
        if (Glib::path_is_absolute(
                desktop_file_name))
        {
            return Gio::DesktopAppInfo::
                create_from_filename(
                    desktop_file_name);
        }

        return Gio::DesktopAppInfo::create(
            desktop_file_name);
    }
    catch (const Glib::Error &)
    {
        return {};
    }
}

std::string normalized_application_id(
    std::string value)
{
    if (value.empty())
        return {};

    if (Glib::path_is_absolute(value))
        value = Glib::path_get_basename(value);

    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(
                std::tolower(character));
        });

    constexpr std::string_view suffix =
        ".desktop";
    if (value.size() >= suffix.size() &&
        value.compare(
            value.size() - suffix.size(),
            suffix.size(),
            suffix) == 0)
    {
        value.erase(
            value.size() - suffix.size());
    }

    return value;
}

std::optional<int> parse_integer(
    const std::string &text)
{
    const auto first = std::find_if_not(
        text.begin(),
        text.end(),
        [](unsigned char character)
        {
            return std::isspace(character);
        });
    const auto last = std::find_if_not(
        text.rbegin(),
        text.rend(),
        [](unsigned char character)
        {
            return std::isspace(character);
        }).base();

    if (first >= last)
        return std::nullopt;

    const auto begin =
        text.data() +
        std::distance(text.begin(), first);
    const auto end =
        text.data() +
        std::distance(text.begin(), last);
    int value = 0;
    const auto result = std::from_chars(
        begin,
        end,
        value);
    return result.ec == std::errc{} &&
                   result.ptr == end
               ? std::optional<int>{value}
               : std::nullopt;
}

std::optional<std::pair<int, int>> parse_pair(
    const std::string &text)
{
    const auto separator =
        text.find_first_of("xX");
    if (separator == std::string::npos)
        return std::nullopt;

    const auto first =
        parse_integer(text.substr(0, separator));
    const auto second =
        parse_integer(text.substr(separator + 1));
    if (!first || !second)
        return std::nullopt;

    return std::make_pair(*first, *second);
}

Glib::RefPtr<Gdk::AppLaunchContext>
application_launch_context(
    const Glib::RefPtr<Gio::AppInfo> &application)
{
    const auto display =
        Gdk::Display::get_default();
    if (!display)
        return {};

    auto context =
        display->get_app_launch_context();
    if (!context)
        return {};

    context->set_timestamp(
        gtk_get_current_event_time());
    if (application)
        context->set_icon(
            application->get_icon());

    prepare_application_launch_context(
        G_APP_LAUNCH_CONTEXT(
            context->gobj()));
    return context;
}

Glib::RefPtr<Gio::AppInfo>
application_with_parameters(
    const Glib::RefPtr<Gio::DesktopAppInfo> &application,
    const std::string &parameters)
{
    if (parameters.empty())
        return application;

    const auto executable =
        application->get_executable();
    if (executable.empty())
        return {};

    std::string command =
        Glib::shell_quote(executable);
    for (const auto &argument :
         Glib::shell_parse_argv(parameters))
    {
        command += " " +
                   Glib::shell_quote(argument);
    }

    return Gio::AppInfo::create_from_commandline(
        command,
        application->get_display_name(),
        Gio::APP_INFO_CREATE_NONE);
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
    CaptureWindowProvider capture_window,
    WindowRegistry *window_registry)
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
      m_capture_window(std::move(capture_window)),
      m_window_registry(window_registry)
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

    m_launch_button.signal_clicked().connect(
        sigc::mem_fun(
            *this,
            &DockSessionItem::launcher));

    m_remove_button.signal_clicked().connect(
        [this]()
        {
            m_remove_requested.emit();
        });
}

DockSessionItem::~DockSessionItem()
{
    stop_launch_tracking();
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

WindowPlacement DockSessionItem::placement() const
{
    WindowPlacement result;

    auto workspace = m_workspace.get_text();
    const auto separator = workspace.find(',');
    if (separator != Glib::ustring::npos)
        workspace.erase(separator);

    const auto workspace_number =
        parse_integer(workspace.raw());
    if (workspace_number &&
        *workspace_number > 0)
    {
        result.workspace_number =
            static_cast<unsigned int>(
                *workspace_number);
    }

    const auto dimensions =
        parse_pair(m_dimensions.get_text());
    const auto position =
        parse_pair(m_position.get_text());
    if (dimensions &&
        position &&
        dimensions->first > 0 &&
        dimensions->second > 0)
    {
        result.frame_geometry =
            WindowGeometry{
                position->first,
                position->second,
                dimensions->first,
                dimensions->second};
    }

    return result;
}

void DockSessionItem::launcher()
{
    const std::string desktop_file =
        m_desktop_file.get_text();
    const auto application =
        find_desktop_application(desktop_file);
    if (!application)
    {
        g_warning(
            "Cannot launch Session item: desktop file '%s' is unavailable",
            desktop_file.c_str());
        return;
    }

    Glib::RefPtr<Gio::AppInfo> launch_application;
    try
    {
        launch_application =
            application_with_parameters(
                application,
                m_parameters.get_text());
    }
    catch (const Glib::Error &error)
    {
        g_warning(
            "Cannot parse Session parameters for %s: %s",
            application->get_display_name().c_str(),
            error.what().c_str());
        return;
    }

    if (!launch_application)
        return;

    stop_launch_tracking();
    m_launch_placement = placement();
    m_launched_application_id =
        normalized_application_id(
            application->get_id().empty()
                ? desktop_file
                : application->get_id());
    m_launched_title =
        m_app_title.get_entry()->get_text();

    if (m_window_registry &&
        m_window_registry->capabilities().can_place)
    {
        for (const auto &window :
             m_window_registry->windows())
        {
            m_windows_before_launch.insert(
                window.id);
        }

        m_launch_window_changed =
            m_window_registry->signal_changed().connect(
                sigc::mem_fun(
                    *this,
                    &DockSessionItem::
                        on_window_registry_changed));
        m_launch_timeout =
            Glib::signal_timeout().connect_seconds(
                sigc::mem_fun(
                    *this,
                    &DockSessionItem::
                        on_launch_timeout),
                15);
    }

    try
    {
        std::vector<Glib::RefPtr<Gio::File>> files;
        launch_application->launch(
            files,
            application_launch_context(
                application));
    }
    catch (const Glib::Error &error)
    {
        stop_launch_tracking();
        g_warning(
            "Cannot launch Session item %s: %s",
            application->get_display_name().c_str(),
            error.what().c_str());
    }
}

const ManagedWindow *DockSessionItem::launched_window() const
{
    if (!m_window_registry)
        return nullptr;

    const ManagedWindow *candidate = nullptr;
    for (const auto &window :
         m_window_registry->windows())
    {
        if (normalized_application_id(
                window.desktop_file_name) !=
            m_launched_application_id)
        {
            continue;
        }

        if (m_windows_before_launch.count(
                window.id) == 0)
        {
            if (!m_launched_title.empty() &&
                window.caption ==
                    m_launched_title)
            {
                return &window;
            }
            if (!candidate || window.active)
                candidate = &window;
        }
    }

    if (candidate)
        return candidate;

    if (m_window_registry->active_window())
    {
        const auto *active =
            m_window_registry->find_window(
                *m_window_registry->active_window());
        if (active &&
            normalized_application_id(
                active->desktop_file_name) ==
                m_launched_application_id)
        {
            return active;
        }
    }

    return nullptr;
}

void DockSessionItem::on_window_registry_changed()
{
    const auto *window = launched_window();
    if (!window)
        return;

    if (!m_window_registry->place_window(
            window->id,
            m_launch_placement))
    {
        g_warning(
            "Cannot place launched Session window '%s'",
            window->id.c_str());
    }

    stop_launch_tracking();
}

bool DockSessionItem::on_launch_timeout()
{
    m_launch_window_changed.disconnect();
    m_windows_before_launch.clear();
    return false;
}

void DockSessionItem::stop_launch_tracking()
{
    m_launch_window_changed.disconnect();
    m_launch_timeout.disconnect();
    m_windows_before_launch.clear();
}

sigc::signal<void> &DockSessionItem::signal_remove_requested()
{
    return m_remove_requested;
}
