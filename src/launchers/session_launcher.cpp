// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// session_launcher.cpp
//
// Implementation overview:
// Resolves stored Session items to installed applications, launches them, and
// places the resulting windows once they appear.
//
// Important implementation decisions:
// - Desktop entries resolve by path, desktop ID, and finally by a normalized
//   scan of installed applications, because backends report whichever identity
//   they can: a desktop ID, a GTK application ID, a WM_CLASS, or an executable
//   name, and only the first is directly loadable.
// - Parameters are appended to the entry's complete Exec line with field codes
//   removed. Keeping only the executable name would discard wrappers such as
//   "flatpak run", "env", and "sh -c" and launch a different program.
// - Browser launches always request a new top-level window. A stored new-tab
//   switch is rewritten, because reusing an unrelated browser window would
//   leave the Session with no new window to identify or place.
// - Placement waits for a window that did not exist before the launch, so an
//   application that reuses a running process never moves the user's existing
//   window.
// - Several items may be in flight at once, so pending placements are a list
//   drained by one registry subscription.
//
// ------------------------------------------------------------

#include "session_launcher.h"

#include "presentation/presentation_selector.h"
#include "windowing/window_registry.h"

#include <gio/gdesktopappinfo.h>
#include <giomm/desktopappinfo.h>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>
#include <glibmm/shell.h>
#include <gdkmm/applaunchcontext.h>
#include <gdkmm/display.h>
#include <gtk/gtk.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <string_view>
#include <utility>

namespace
{
// Placement is abandoned when no matching window appears within this window.
constexpr unsigned int TRACKING_SECONDS = 15;

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

// GNOME, KWin, and EWMH report whichever identity they can resolve. Only a
// desktop ID is directly loadable, so fall back to a normalized scan over the
// installed entries before declaring the application unavailable.
Glib::RefPtr<Gio::DesktopAppInfo>
find_installed_application(
    const std::string &requested)
{
    const auto normalized =
        normalized_application_id(requested);
    if (normalized.empty())
        return {};

    const auto matches =
        [&normalized](
            const std::string &value)
    {
        return !value.empty() &&
               normalized_application_id(
                   value) == normalized;
    };

    for (const auto &candidate :
         Gio::AppInfo::get_all())
    {
        const auto application =
            Glib::RefPtr<Gio::DesktopAppInfo>::
                cast_dynamic(candidate);
        if (!application)
            continue;

        if (matches(application->get_id()) ||
            matches(application->get_name()) ||
            matches(
                application->get_display_name()) ||
            matches(
                application->get_executable()))
        {
            return application;
        }

        const auto *startup_wm_class =
            g_desktop_app_info_get_startup_wm_class(
                application->gobj());
        if (startup_wm_class &&
            matches(startup_wm_class))
        {
            return application;
        }
    }

    return {};
}

}

Glib::RefPtr<Gio::DesktopAppInfo>
find_session_application(
    const std::string &desktop_file_name)
{
    if (desktop_file_name.empty())
        return {};

    try
    {
        if (Glib::path_is_absolute(
                desktop_file_name))
        {
            if (const auto application =
                    Gio::DesktopAppInfo::
                        create_from_filename(
                            desktop_file_name))
            {
                return application;
            }
        }
        else
        {
            if (const auto application =
                    Gio::DesktopAppInfo::create(
                        desktop_file_name))
            {
                return application;
            }

            if (const auto application =
                    Gio::DesktopAppInfo::create(
                        desktop_file_name +
                        ".desktop"))
            {
                return application;
            }
        }
    }
    catch (const Glib::Error &)
    {
    }

    return find_installed_application(
        desktop_file_name);
}

namespace
{
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

std::vector<std::string> tracking_application_ids(
    const Glib::RefPtr<Gio::DesktopAppInfo> &application,
    const std::string &requested)
{
    std::vector<std::string> identifiers;

    const auto add =
        [&identifiers](const std::string &value)
    {
        const auto normalized =
            normalized_application_id(value);

        if (normalized.empty() ||
            std::find(
                identifiers.begin(),
                identifiers.end(),
                normalized) != identifiers.end())
        {
            return;
        }

        identifiers.push_back(normalized);
    };

    add(requested);
    add(application->get_id());
    add(application->get_name());
    add(application->get_display_name());
    add(application->get_executable());

    const auto *startup_wm_class =
        g_desktop_app_info_get_startup_wm_class(
            application->gobj());
    if (startup_wm_class)
        add(startup_wm_class);

    return identifiers;
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

// Desktop-entry field codes are expanded by GIO from the launched files. A
// Session launches without files, so drop them rather than passing literal
// "%u" text to the application.
std::string without_field_codes(
    const std::string &argument)
{
    constexpr std::string_view codes =
        "fFuUdDnNickvm";

    std::string result;
    result.reserve(argument.size());

    for (std::size_t index = 0;
         index < argument.size();
         ++index)
    {
        if (argument[index] != '%' ||
            index + 1 == argument.size())
        {
            result += argument[index];
            continue;
        }

        const auto code = argument[index + 1];
        if (code == '%')
        {
            result += '%';
            ++index;
            continue;
        }

        if (codes.find(code) !=
            std::string_view::npos)
        {
            ++index;
            continue;
        }

        result += argument[index];
    }

    return result;
}

// A Session restores windows, so a browser must create a new top-level window
// rather than adding a tab to whatever window the user already has. Firefox
// declares no desktop actions at all, and Chrome's "new-window" action drops
// the URL, so neither
// can be reached through g_desktop_app_info_launch_action(). The browser's own
// flag is the only route. Categories is the entry's own declaration; handling
// the http scheme catches an entry that omits the category.
bool is_web_browser(
    const Glib::RefPtr<Gio::DesktopAppInfo>
        &application)
{
    if (!application)
        return false;

    const auto *categories =
        g_desktop_app_info_get_categories(
            application->gobj());

    if (categories &&
        std::string(categories)
                .find("WebBrowser") !=
            std::string::npos)
    {
        return true;
    }

    for (const auto &type :
         application->get_supported_types())
    {
        if (type == "x-scheme-handler/http" ||
            type == "x-scheme-handler/https")
        {
            return true;
        }
    }

    return false;
}

// Every mainstream browser spells this the same way: the Firefox family, the
// Chromium family, and Epiphany all accept "--new-window <url>". This is the
// browser instance a Session owns; forcing a separate OS process would either
// be ignored by multi-process browsers or collide with the user's locked
// profile.
constexpr const char *NEW_WINDOW_ARGUMENT =
    "--new-window";

bool opens_new_window(
    const std::string &argument)
{
    constexpr const char *window_targets[] = {
        "--new-window",
        "-new-window",
        "--incognito",
        "--private-window",
        "-private-window"};

    for (const auto *target : window_targets)
    {
        if (argument == target)
            return true;
    }

    return false;
}

bool requests_new_tab(
    const std::string &argument)
{
    return argument == "--new-tab" ||
           argument == "-new-tab";
}

// gedit is a single-instance GApplication by default. A Session item must
// never be routed into an already-running editor: use its documented
// standalone switch so every stored item gets a fresh process and window.
bool requires_standalone_instance(
    const Glib::RefPtr<Gio::DesktopAppInfo> &application)
{
    if (!application)
        return false;

    const auto is_gedit =
        [](const std::string &value)
        {
            return normalized_application_id(value) ==
                   "gedit";
        };

    return is_gedit(application->get_id()) ||
           is_gedit(application->get_name()) ||
           is_gedit(application->get_executable());
}

Glib::RefPtr<Gio::AppInfo>
application_with_parameters(
    const Glib::RefPtr<Gio::DesktopAppInfo> &application,
    const std::string &parameters)
{
    const bool browser =
        is_web_browser(application);

    if (parameters.empty() && !browser)
        return application;

    // The entry's own Exec line carries wrappers and arguments such as
    // "flatpak run --branch=stable" or "env GDK_BACKEND=x11". Keeping only the
    // executable name would launch a different program, so rebuild the command
    // from the complete Exec line.
    const auto commandline =
        application->get_commandline();
    if (commandline.empty())
        return {};

    std::string command;
    const auto append_argument =
        [&command](
            const std::string &argument)
    {
        if (!command.empty())
            command += " ";
        command +=
            Glib::shell_quote(argument);
    };

    for (const auto &argument :
         Glib::shell_parse_argv(commandline))
    {
        const auto value =
            without_field_codes(argument);
        if (value.empty())
            continue;

        append_argument(value);
    }

    if (command.empty())
        return {};

    std::vector<std::string> user_arguments;

    if (!parameters.empty())
    {
        for (const auto &argument :
             Glib::shell_parse_argv(parameters))
        {
            user_arguments.push_back(argument);
        }
    }

    // A new window is part of the Session contract, not an optional user
    // preference. Rewrite an explicit new-tab request and otherwise put the
    // flag ahead of the stored URL so that URL becomes its argument.
    const bool has_window_target =
        browser &&
        std::any_of(
            user_arguments.begin(),
            user_arguments.end(),
            [](const std::string &argument)
            {
                return opens_new_window(argument) ||
                       requests_new_tab(argument);
            });

    if (browser && !has_window_target)
    {
        append_argument(
            NEW_WINDOW_ARGUMENT);
    }

    if (requires_standalone_instance(application))
        append_argument("--standalone");

    for (auto argument : user_arguments)
    {
        if (browser && requests_new_tab(argument))
            argument = NEW_WINDOW_ARGUMENT;

        append_argument(argument);
    }

    return Gio::AppInfo::create_from_commandline(
        command,
        application->get_display_name(),
        g_desktop_app_info_get_boolean(
            application->gobj(),
            "Terminal")
            ? Gio::APP_INFO_CREATE_NEEDS_TERMINAL
            : Gio::APP_INFO_CREATE_NONE);
}

WindowPlacement placement_for(
    const SessionItemRecord &item)
{
    WindowPlacement result;

    auto workspace = item.workspace;
    const auto separator = workspace.find(',');
    if (separator != std::string::npos)
        workspace.erase(separator);

    const auto workspace_number =
        parse_integer(workspace);
    if (workspace_number &&
        *workspace_number > 0)
    {
        result.workspace_number =
            static_cast<unsigned int>(
                *workspace_number);
    }

    const auto dimensions =
        parse_pair(item.dimensions);
    const auto position =
        parse_pair(item.position);
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
}

SessionLauncher::SessionLauncher(
    WindowRegistry *window_registry)
    : m_window_registry(window_registry)
{
}

SessionLauncher::~SessionLauncher()
{
    stop_tracking();
}

bool SessionLauncher::can_place() const
{
    return m_window_registry &&
           m_window_registry->capabilities()
               .can_place;
}

bool SessionLauncher::tracks_windows() const
{
    return m_window_registry != nullptr;
}

Glib::ustring SessionLauncher::launch(
    const SessionItemRecord &item,
    const std::string &tag)
{
    if (item.desktop_file.empty())
    {
        return _("This Session item has no application.");
    }

    const auto application =
        find_session_application(
            item.desktop_file);
    if (!application)
    {
        return Glib::ustring::compose(
            _("No installed application matches \"%1\"."),
            item.desktop_file);
    }

    Glib::RefPtr<Gio::AppInfo> launch_application;
    try
    {
        launch_application =
            application_with_parameters(
                application,
                item.parameters);
    }
    catch (const Glib::Error &error)
    {
        return Glib::ustring::compose(
            _("The parameters cannot be parsed: %1"),
            error.what());
    }

    if (!launch_application)
    {
        return Glib::ustring::compose(
            _("%1 does not declare a command that accepts parameters."),
            application->get_display_name());
    }

    // Identifying the launched window is what attributes it. Placement is one
    // use of that identity; reporting it to the caller through the tag is the
    // other, and it is the only way a Session ever learns that a live window
    // is its own.
    std::optional<PendingPlacement> pending_launch;
    if (m_window_registry)
    {
        PendingPlacement pending;
        pending.application_ids =
            tracking_application_ids(
                application,
                item.desktop_file);
        pending.title = item.title;
        pending.tag = tag;
        pending.placement = placement_for(item);

        for (const auto &window :
             m_window_registry->windows())
        {
            pending.windows_before_launch
                .insert(window.id);
        }

        // Register only after GIO accepted the launch below. Otherwise a
        // failed command occupies the tracking queue until its deadline and
        // stalls a serialized whole-Session launch.
        pending_launch = std::move(pending);
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
        return Glib::ustring::compose(
            _("%1 cannot be launched: %2"),
            application->get_display_name(),
            error.what());
    }


    if (pending_launch)
        begin_tracking(std::move(*pending_launch));

    return {};
}

void SessionLauncher::begin_tracking(
    PendingPlacement pending)
{
    m_pending.push_back(std::move(pending));

    if (!m_window_changed.connected())
    {
        m_window_changed =
            m_window_registry->signal_changed()
                .connect(
                    sigc::mem_fun(
                        *this,
                        &SessionLauncher::
                            on_window_registry_changed));
    }

    // Restart the deadline so a Session whose applications start slowly is
    // not cut short by an earlier item's timer.
    m_timeout.disconnect();
    m_timeout =
        Glib::signal_timeout()
            .connect_seconds(
                sigc::mem_fun(
                    *this,
                    &SessionLauncher::
                        on_tracking_timeout),
                TRACKING_SECONDS);
}

void SessionLauncher::
    on_window_registry_changed()
{
    if (!m_window_registry)
        return;

    // Several launches can be pending at once (for example, a Session with
    // three browser rows). A registry update must not let two pending rows
    // consume the same newly-created window. Keep reservations across updates
    // because the backend can announce each window in a separate callback.
    std::set<WindowId> claimed_windows =
        m_claimed_window_ids;

    for (auto pending = m_pending.begin();
         pending != m_pending.end();)
    {
        const ManagedWindow *match = nullptr;
        const ManagedWindow *candidate = nullptr;

        for (const auto &window :
             m_window_registry->windows())
        {
            const auto window_application_id =
                normalized_application_id(
                    window.desktop_file_name);

            if (std::find(
                    pending->application_ids.begin(),
                    pending->application_ids.end(),
                    window_application_id) ==
                pending->application_ids.end())
            {
                continue;
            }

            // Only a window that appeared after the launch may be moved. An
            // already-open window belongs to the user.
            if (pending->windows_before_launch
                    .count(window.id) != 0)
            {
                continue;
            }

            if (claimed_windows.count(window.id) != 0)
                continue;

            if (!pending->title.empty() &&
                window.caption == pending->title)
            {
                match = &window;
                break;
            }

            if (!candidate || window.active)
                candidate = &window;
        }

        if (!match)
            match = candidate;

        if (!match)
        {
            ++pending;
            continue;
        }

        claimed_windows.insert(match->id);
        m_claimed_window_ids.insert(match->id);

        if (can_place() &&
            !m_window_registry->place_window(
                match->id,
                pending->placement))
        {
            g_warning(
                "Cannot place launched Session window '%s'",
                match->id.c_str());
        }

        if (!pending->tag.empty())
        {
            m_window_identified.emit(
                pending->tag,
                match->id);
            m_launch_finished.emit(pending->tag);
        }

        pending = m_pending.erase(pending);
    }

    if (m_pending.empty())
        stop_tracking();
}

bool SessionLauncher::on_tracking_timeout()
{
    for (const auto &pending : m_pending)
    {
        if (!pending.tag.empty())
            m_launch_finished.emit(pending.tag);
    }
    m_pending.clear();
    m_claimed_window_ids.clear();
    m_window_changed.disconnect();
    return false;
}

sigc::signal<
    void,
    std::string,
    WindowId> &
SessionLauncher::signal_window_identified()
{
    return m_window_identified;
}

sigc::signal<void, std::string> &
SessionLauncher::signal_launch_finished()
{
    return m_launch_finished;
}

void SessionLauncher::stop_tracking()
{
    m_window_changed.disconnect();
    m_timeout.disconnect();
    m_pending.clear();
    m_claimed_window_ids.clear();
}
