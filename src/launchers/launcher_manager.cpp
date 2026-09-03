// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// launcher_manager.cpp
//
// Implementation overview:
// Implements launcher-file persistence, saved Session persistence, installed
// application lookup, desktop-ID normalization, and application-cache
// invalidation.
//
// Important implementation decisions:
// - Stored order is preserved while duplicate identities are removed.
// - Desktop IDs compare case-insensitively with a canonical suffix.
// - docklight.data holds the mixed visual order first and full Session
//   definitions at the bottom. Every read and write carries both, so a
//   launcher reorder cannot drop Sessions and a Session save cannot drop the
//   order.
// - Session values are key=value lines because desktop IDs and window titles
//   are arbitrary text that may contain spaces, pipes, or brackets.
// - Writes replace the complete file to keep reorder and save atomic.
// - Gio application enumeration is cached until its monitor reports change.
//
// ------------------------------------------------------------

#include "launcher_manager.h"

#include <gio/gdesktopappinfo.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iterator>
#include <ostream>
#include <string_view>
#include <utility>

namespace
{

// Section markers for the Session block that follows the attached-launcher
// list in docklight.data.
constexpr const char *SESSION_SECTION_PREFIX = "session:";
constexpr const char *SESSION_ITEM_SECTION = "item";
constexpr const char *DOCK_ORDER_SECTION = "dock-order";
constexpr const char *DOCK_ORDER_ITEM = "item";

std::string trimmed(
    const std::string &value)
{
    const auto first =
        std::find_if_not(
            value.begin(),
            value.end(),
            [](unsigned char character)
            {
                return std::isspace(
                    character);
            });

    const auto last =
        std::find_if_not(
            value.rbegin(),
            value.rend(),
            [](unsigned char character)
            {
                return std::isspace(
                    character);
            })
            .base();

    if (first >= last)
        return {};

    return std::string(first, last);
}

// The store is line-based, so an embedded newline or carriage return would
// silently split one value into two records. Nothing else needs escaping
// because a key is everything before the first '='.
std::string single_line(
    std::string value)
{
    std::replace(
        value.begin(),
        value.end(),
        '\n',
        ' ');
    std::replace(
        value.begin(),
        value.end(),
        '\r',
        ' ');

    return trimmed(value);
}

void write_session_value(
    std::ostream &file,
    const char *key,
    const std::string &value)
{
    if (value.empty())
        return;

    file << key
         << '='
         << single_line(value)
         << '\n';
}

}

LauncherManager::LauncherManager(
    std::string data_path)
    : m_data_path(
          data_path.empty()
              ? Glib::build_filename(
                    Glib::get_user_config_dir(),
                    "docklight6",
                    "docklight.data")
              : std::move(data_path))
{
    m_app_info_monitor =
        g_app_info_monitor_get();

    if (m_app_info_monitor)
    {
        m_app_info_changed_handler =
            g_signal_connect(
                m_app_info_monitor,
                "changed",
                G_CALLBACK(
                    LauncherManager::
                        on_applications_changed),
                this);
    }
}

LauncherManager::~LauncherManager()
{
    if (m_app_info_monitor &&
        m_app_info_changed_handler != 0)
    {
        g_signal_handler_disconnect(
            m_app_info_monitor,
            m_app_info_changed_handler);
    }
}

std::vector<std::string>
LauncherManager::attached_ids() const
{
    return read_config();
}

Glib::RefPtr<Gio::AppInfo>
LauncherManager::find_application(
    const std::string &desktop_id) const
{
    const auto requested =
        trimmed(desktop_id);

    if (requested.empty())
        return {};

    try
    {
        if (requested.find('/') !=
                std::string::npos ||
            requested.find('\\') !=
                std::string::npos)
        {
            auto app =
                Gio::DesktopAppInfo::
                    create_from_filename(
                        requested);

            if (app)
                return app;
        }

        auto app =
            Gio::DesktopAppInfo::create(
                requested);

        if (app)
            return app;
    }
    catch (const Glib::Error &)
    {
    }

    const auto normalized_requested =
        normalize_desktop_id(
            requested);

    for (const auto &app :
         applications())
    {
        if (!app)
            continue;

        const auto matches =
            [&normalized_requested](
                const std::string &value)
            {
                return normalize_desktop_id(
                           value) ==
                       normalized_requested;
            };

        if (matches(app->get_id()) ||
            matches(app->get_name()) ||
            matches(app->get_display_name()) ||
            matches(app->get_executable()))
        {
            return app;
        }

        const auto icon = app->get_icon();

        if (icon &&
            G_IS_THEMED_ICON(icon->gobj()))
        {
            const auto icon_names =
                g_themed_icon_get_names(
                    G_THEMED_ICON(
                        icon->gobj()));

            for (int index = 0;
                 icon_names && icon_names[index];
                 ++index)
            {
                if (matches(icon_names[index]))
                    return app;
            }
        }

        if (G_IS_DESKTOP_APP_INFO(
                app->gobj()))
        {
            const auto startup_wm_class =
                g_desktop_app_info_get_startup_wm_class(
                    G_DESKTOP_APP_INFO(
                        app->gobj()));

            if (startup_wm_class &&
                matches(startup_wm_class))
            {
                return app;
            }
        }
    }

    return {};
}

void LauncherManager::
    on_applications_changed(
        GAppInfoMonitor *,
        gpointer user_data)
{
    auto *manager =
        static_cast<LauncherManager *>(
            user_data);

    manager->m_applications.clear();
    manager->m_applications_loaded =
        false;
}

const std::vector<
    Glib::RefPtr<Gio::AppInfo>> &
LauncherManager::applications() const
{
    if (!m_applications_loaded)
    {
        m_applications =
            Gio::AppInfo::get_all();
        m_applications_loaded = true;
    }

    return m_applications;
}

bool LauncherManager::set_attached(
    const std::string &desktop_id,
    bool attached)
{
    if (attached &&
        is_transient_window_id(
            desktop_id))
    {
        g_warning(
            "Cannot attach transient window identity '%s'",
            desktop_id.c_str());
        return false;
    }

    const auto normalized_id =
        normalize_resolved_id(
            desktop_id);

    if (normalized_id.empty())
        return false;

    auto ids = read_config();

    const auto current =
        std::find_if(
            ids.begin(),
            ids.end(),
            [this, &normalized_id](
                const std::string &candidate)
            {
                return normalize_resolved_id(
                           candidate) ==
                       normalized_id;
            });

    if (attached)
    {
        if (current != ids.end())
            return true;

        const auto app =
            find_application(
                desktop_id);

        ids.push_back(
            app &&
                    !app->get_id().empty()
                ? app->get_id()
                : trimmed(desktop_id));
    }
    else
    {
        if (current == ids.end())
            return true;

        ids.erase(current);
    }

    return write_config(ids);
}

bool LauncherManager::reorder_attached(
    const std::vector<std::string>
        &desktop_ids)
{
    const auto current_ids =
        read_config();

    std::vector<std::string> ordered_ids;
    ordered_ids.reserve(
        current_ids.size());

    for (const auto &desktop_id :
         desktop_ids)
    {
        const auto normalized_id =
            normalize_resolved_id(
                desktop_id);

        const auto current =
            std::find_if(
                current_ids.begin(),
                current_ids.end(),
                [this, &normalized_id](
                    const std::string
                        &candidate)
                {
                    return normalize_resolved_id(
                               candidate) ==
                           normalized_id;
                });

        const bool already_added =
            std::any_of(
                ordered_ids.begin(),
                ordered_ids.end(),
                [this, &normalized_id](
                    const std::string
                        &candidate)
                {
                    return normalize_resolved_id(
                               candidate) ==
                           normalized_id;
                });

        if (current == current_ids.end() ||
            already_added)
        {
            return false;
        }

        ordered_ids.push_back(
            *current);
    }

    for (const auto &current_id :
         current_ids)
    {
        const auto normalized_id =
            normalize_resolved_id(
                current_id);

        const bool already_added =
            std::any_of(
                ordered_ids.begin(),
                ordered_ids.end(),
                [this, &normalized_id](
                    const std::string
                        &candidate)
                {
                    return normalize_resolved_id(
                               candidate) ==
                           normalized_id;
                });

        if (!already_added)
            ordered_ids.push_back(
                current_id);
    }

    return ordered_ids == current_ids ||
           write_config(ordered_ids);
}

bool LauncherManager::is_attached(
    const std::string &desktop_id) const
{
    const auto normalized_id =
        normalize_resolved_id(
            desktop_id);

    if (normalized_id.empty())
        return false;

    const auto ids = read_config();

    return std::any_of(
        ids.begin(),
        ids.end(),
        [this, &normalized_id](
            const std::string &candidate)
        {
            return normalize_resolved_id(
                       candidate) ==
                   normalized_id;
        });
}

std::string
LauncherManager::normalize_resolved_id(
    const std::string &desktop_id) const
{
    const auto app =
        find_application(
            desktop_id);

    return normalize_desktop_id(
        app &&
                !app->get_id().empty()
            ? app->get_id()
            : desktop_id);
}

std::string
LauncherManager::normalize_desktop_id(
    const std::string &desktop_id)
{
    auto normalized =
        trimmed(desktop_id);

    const auto separator =
        normalized.find_last_of(
            "/\\");

    if (separator != std::string::npos)
    {
        normalized.erase(
            0,
            separator + 1);
    }

    if (normalized.empty())
        return {};

    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char character)
        {
            if (std::isspace(character) ||
                character == '_')
            {
                return '-';
            }

            return static_cast<char>(
                std::tolower(character));
        });

    constexpr char suffix[] =
        ".desktop"; // Desktop-entry filename suffix

    if (normalized.size() <
            sizeof(suffix) - 1 ||
        normalized.compare(
            normalized.size() -
                (sizeof(suffix) - 1),
            sizeof(suffix) - 1,
            suffix) != 0)
    {
        normalized += suffix;
    }

    return normalized;
}

bool LauncherManager::is_transient_window_id(
    const std::string &desktop_id)
{
    const auto normalized =
        normalize_desktop_id(
            desktop_id);

    constexpr char prefix[] = "window:";
    constexpr char suffix[] = ".desktop";
    constexpr std::size_t prefix_length =
        sizeof(prefix) - 1;
    constexpr std::size_t suffix_length =
        sizeof(suffix) - 1;

    if (normalized.size() <=
            prefix_length + suffix_length ||
        normalized.compare(
            0,
            prefix_length,
            prefix) != 0 ||
        normalized.compare(
            normalized.size() - suffix_length,
            suffix_length,
            suffix) != 0)
    {
        return false;
    }

    return std::all_of(
        normalized.begin() + prefix_length,
        normalized.end() - suffix_length,
        [](unsigned char character)
        {
            return std::isdigit(character);
        });
}

// docklight.data starts with the mixed visual order: bare desktop IDs and
// lightweight [session:name] markers. Complete Session definitions use the
// same header plus key=value contents and remain together at the bottom.
// Values may contain spaces, pipes, or brackets without escaping, which
// matters because desktop IDs such as "mullvad browser.desktop" and window
// titles are arbitrary text.
LauncherManager::StoredData
LauncherManager::read_data() const
{
    StoredData data;
    bool removed_transient_id = false;
    std::ifstream file(m_data_path);
    std::vector<std::string> lines;
    std::string line;

    while (std::getline(file, line))
    {
        line = trimmed(line);

        if (line.empty() || line[0] == '#')
            continue;

        lines.push_back(line);
    }

    const auto session_name =
        [](const std::string &candidate)
            -> std::string
    {
        if (candidate.size() < 3 ||
            candidate.front() != '[' ||
            candidate.back() != ']')
        {
            return {};
        }

        const auto section = trimmed(
            candidate.substr(
                1,
                candidate.size() - 2));
        constexpr std::string_view prefix =
            SESSION_SECTION_PREFIX;

        if (section.size() <= prefix.size() ||
            section.compare(
                0,
                prefix.size(),
                prefix) != 0)
        {
            return {};
        }

        return trimmed(
            section.substr(prefix.size()));
    };

    // The same [session:name] syntax is used for a lightweight marker in the
    // leading dock order and for the full definition at the bottom. A header
    // owns a definition only when its block contains icon= or [item].
    std::vector<bool> is_definition(
        lines.size(),
        false);

    for (std::size_t index = 0;
         index < lines.size();
         ++index)
    {
        if (session_name(lines[index]).empty())
            continue;

        for (std::size_t content = index + 1;
             content < lines.size();
             ++content)
        {
            if (!session_name(lines[content]).empty() ||
                lines[content] ==
                    std::string("[") +
                        DOCK_ORDER_SECTION + "]")
            {
                break;
            }

            if (lines[content] ==
                    std::string("[") +
                        SESSION_ITEM_SECTION + "]" ||
                lines[content].compare(
                    0,
                    std::char_traits<char>::length(
                        "icon="),
                    "icon=") == 0)
            {
                is_definition[index] = true;
                break;
            }
        }
    }

    bool in_definition = false;
    bool in_legacy_dock_order = false;
    std::size_t current_session = 0;
    std::vector<std::string> legacy_dock_order;

    for (std::size_t index = 0;
         index < lines.size();
         ++index)
    {
        line = lines[index];

        if (line.front() == '[' &&
            line.back() == ']')
        {
            const auto section =
                trimmed(line.substr(
                    1,
                    line.size() - 2));

            if (section == DOCK_ORDER_SECTION)
            {
                in_definition = false;
                in_legacy_dock_order = true;
                continue;
            }

            if (in_legacy_dock_order)
                continue;

            if (section == SESSION_ITEM_SECTION)
            {
                if (in_definition &&
                    current_session <
                        data.sessions.size())
                {
                    data.sessions[current_session]
                        .items.emplace_back();
                }
                continue;
            }

            const auto name = session_name(line);
            if (!name.empty())
            {
                if (is_definition[index])
                {
                    const auto existing = std::find_if(
                        data.sessions.begin(),
                        data.sessions.end(),
                        [&name](
                            const SessionRecord &session)
                        {
                            return session.name == name;
                        });

                    if (existing == data.sessions.end())
                    {
                        SessionRecord record;
                        record.name = name;
                        data.sessions.push_back(
                            std::move(record));
                        current_session =
                            data.sessions.size() - 1;
                    }
                    else
                    {
                        current_session =
                            static_cast<std::size_t>(
                                std::distance(
                                    data.sessions.begin(),
                                    existing));
                        data.sessions[current_session] =
                            SessionRecord{};
                        data.sessions[current_session].name =
                            name;
                    }

                    in_definition = true;
                }
                else
                {
                    data.dock_order.push_back(
                        std::string(
                            SESSION_SECTION_PREFIX) +
                        name);
                    in_definition = false;
                }
            }

            continue;
        }

        if (in_legacy_dock_order)
        {
            const auto separator =
                line.find('=');
            if (separator == std::string::npos)
                continue;

            const auto key = trimmed(
                line.substr(0, separator));
            const auto value = trimmed(
                line.substr(separator + 1));
            if (key == DOCK_ORDER_ITEM &&
                !value.empty())
            {
                legacy_dock_order.push_back(value);
            }
            continue;
        }

        if (in_definition &&
            current_session < data.sessions.size())
        {
            const auto separator =
                line.find('=');
            if (separator == std::string::npos)
                continue;

            const auto key = trimmed(
                line.substr(0, separator));
            const auto value = trimmed(
                line.substr(separator + 1));
            auto &session =
                data.sessions[current_session];

            if (session.items.empty())
            {
                if (key == "icon")
                    session.icon = value;
                continue;
            }

            auto &item = session.items.back();

            if (key == "desktop-file")
                item.desktop_file = value;
            else if (key == "title")
                item.title = value;
            else if (key == "parameters")
                item.parameters = value;
            else if (key == "workspace")
                item.workspace = value;
            else if (key == "dimensions")
                item.dimensions = value;
            else if (key == "position")
                item.position = value;

            continue;
        }

        auto &ids = data.desktop_ids;

        if (is_transient_window_id(line))
        {
            removed_transient_id = true;
            g_warning(
                "Removing transient window identity '%s' from attached launchers",
                line.c_str());
            continue;
        }

        const auto normalized_id =
            normalize_desktop_id(line);

        const bool duplicate =
            std::any_of(
                ids.begin(),
                ids.end(),
                [&normalized_id](
                    const std::string
                        &candidate)
                {
                    return normalize_desktop_id(
                               candidate) ==
                           normalized_id;
                });

        if (!duplicate)
        {
            ids.push_back(line);
            data.dock_order.push_back(line);
        }
    }

    // Read the short-lived [dock-order] format so files written by an earlier
    // development build migrate on their next save.
    const bool needs_order_migration =
        !legacy_dock_order.empty();
    if (needs_order_migration)
        data.dock_order =
            std::move(legacy_dock_order);

    // An item without a desktop file names no application. It cannot be
    // launched, matched, or shown, so it is not a Session item at all.
    // Dropping it on read means a malformed or superseded entry cannot keep
    // reappearing as a card that does not correspond to anything.
    for (auto &session : data.sessions)
    {
        const auto empty_item = std::remove_if(
            session.items.begin(),
            session.items.end(),
            [](const SessionItemRecord &item)
            {
                return trimmed(item.desktop_file)
                    .empty();
            });

        if (empty_item != session.items.end())
        {
            g_warning(
                "Dropping %zu Session item(s) without a desktop file from '%s'",
                static_cast<std::size_t>(
                    std::distance(
                        empty_item,
                        session.items.end())),
                session.name.c_str());

            session.items.erase(
                empty_item,
                session.items.end());
        }
    }

    if (removed_transient_id ||
        needs_order_migration)
        write_data(data);

    return data;
}

std::vector<std::string>
LauncherManager::read_config() const
{
    return read_data().desktop_ids;
}

// Rewrites the launcher order while carrying the stored Sessions across, so a
// pin, unpin, or drag reorder cannot discard them.
bool LauncherManager::write_config(
    const std::vector<std::string>
        &desktop_ids) const
{
    StoredData data = read_data();
    data.desktop_ids = desktop_ids;

    // A launcher-only reorder replaces the ordinary slots while leaving any
    // Session markers at their current positions in the mixed sequence.
    std::vector<std::string> order;
    order.reserve(
        desktop_ids.size() +
        data.sessions.size());
    auto next_desktop_id =
        desktop_ids.begin();

    for (const auto &stored_id : data.dock_order)
    {
        if (stored_id.compare(
                0,
                std::char_traits<char>::length(
                    SESSION_SECTION_PREFIX),
                SESSION_SECTION_PREFIX) == 0)
        {
            order.push_back(stored_id);
        }
        else if (next_desktop_id !=
                 desktop_ids.end())
        {
            order.push_back(*next_desktop_id++);
        }
    }

    order.insert(
        order.end(),
        next_desktop_id,
        desktop_ids.end());
    data.dock_order = std::move(order);
    return write_data(data);
}

std::vector<SessionRecord>
LauncherManager::sessions() const
{
    auto data = read_data();
    std::vector<SessionRecord> remaining =
        std::move(data.sessions);
    std::vector<SessionRecord> ordered;
    ordered.reserve(remaining.size());

    for (const auto &desktop_id :
         data.dock_order)
    {
        if (desktop_id.compare(
                0,
                std::char_traits<char>::length(
                    SESSION_SECTION_PREFIX),
                SESSION_SECTION_PREFIX) != 0)
        {
            continue;
        }

        const auto name = desktop_id.substr(
            std::char_traits<char>::length(
                SESSION_SECTION_PREFIX));
        const auto session = std::find_if(
            remaining.begin(),
            remaining.end(),
            [&name](const SessionRecord &candidate)
            {
                return candidate.name == name;
            });

        if (session == remaining.end())
            continue;

        ordered.push_back(std::move(*session));
        remaining.erase(session);
    }

    ordered.insert(
        ordered.end(),
        std::make_move_iterator(remaining.begin()),
        std::make_move_iterator(remaining.end()));
    return ordered;
}

std::vector<std::string>
LauncherManager::session_names() const
{
    std::vector<std::string> names;

    for (const auto &session : sessions())
    {
        names.push_back(session.name);
    }

    return names;
}

std::vector<std::string>
LauncherManager::dock_order() const
{
    const auto data = read_data();

    // Older files have no lightweight markers. Their compatibility order is
    // launcher lines first, then all Session definitions.
    std::vector<std::string> remaining =
        data.desktop_ids;
    for (const auto &session : data.sessions)
    {
        remaining.push_back(
            std::string(SESSION_SECTION_PREFIX) +
            session.name);
    }

    const auto identity =
        [this](const std::string &value)
    {
        if (value.compare(
                0,
                std::char_traits<char>::length(
                    SESSION_SECTION_PREFIX),
                SESSION_SECTION_PREFIX) == 0)
        {
            return value;
        }

        return normalize_resolved_id(value);
    };

    std::vector<std::string> order;
    order.reserve(remaining.size());

    // Ignore stale and duplicate entries, while preserving the canonical
    // spelling stored in the launcher and Session records.
    for (const auto &stored_id : data.dock_order)
    {
        const auto stored_identity =
            identity(stored_id);
        const auto match = std::find_if(
            remaining.begin(),
            remaining.end(),
            [&identity, &stored_identity](
                const std::string &candidate)
            {
                return identity(candidate) ==
                       stored_identity;
            });

        if (match == remaining.end())
            continue;

        order.push_back(*match);
        remaining.erase(match);
    }

    order.insert(
        order.end(),
        remaining.begin(),
        remaining.end());
    return order;
}

bool LauncherManager::reorder_dock_items(
    const std::vector<std::string> &desktop_ids)
{
    StoredData data = read_data();

    std::vector<std::string> expected =
        data.desktop_ids;
    for (const auto &session : data.sessions)
    {
        expected.push_back(
            std::string(SESSION_SECTION_PREFIX) +
            session.name);
    }

    const auto identity =
        [this](const std::string &value)
    {
        if (value.compare(
                0,
                std::char_traits<char>::length(
                    SESSION_SECTION_PREFIX),
                SESSION_SECTION_PREFIX) == 0)
        {
            return value;
        }

        return normalize_resolved_id(value);
    };

    std::vector<std::string> requested;
    requested.reserve(desktop_ids.size());
    for (const auto &desktop_id : desktop_ids)
    {
        const auto requested_id = identity(desktop_id);
        const auto match = std::find_if(
            expected.begin(),
            expected.end(),
            [&identity, &requested_id](
                const std::string &candidate)
            {
                return identity(candidate) ==
                       requested_id;
            });

        if (match == expected.end())
            return false;

        requested.push_back(*match);
        expected.erase(match);
    }

    // Persisted items can be absent from the dock because their application
    // is unavailable or the item limit was reached. Keep those records at the
    // end instead of making every visible drag fail.
    requested.insert(
        requested.end(),
        expected.begin(),
        expected.end());

    data.dock_order = std::move(requested);
    return write_data(data);
}

// Saving replaces the Session with the same name and appends an unknown one,
// keeping the attached launcher order untouched.
bool LauncherManager::save_session(
    const SessionRecord &session)
{
    const auto name = trimmed(session.name);

    if (name.empty())
    {
        g_warning(
            "Cannot save a Session without a name");
        return false;
    }

    StoredData data = read_data();

    SessionRecord stored = session;
    stored.name = name;

    const auto existing = std::find_if(
        data.sessions.begin(),
        data.sessions.end(),
        [&name](const SessionRecord &candidate)
        {
            return candidate.name == name;
        });

    if (existing != data.sessions.end())
        *existing = std::move(stored);
    else
        data.sessions.push_back(
            std::move(stored));

    return write_data(data);
}

// Renaming is keyed by the name that was originally loaded in the editor.
// Updating only by the newly typed name would append a second Session and
// leave the old marker and definition behind.
bool LauncherManager::rename_session(
    const std::string &old_name,
    const SessionRecord &session)
{
    const auto source_name = trimmed(old_name);
    const auto target_name = trimmed(session.name);

    if (source_name.empty() ||
        target_name.empty())
    {
        return false;
    }

    StoredData data = read_data();

    const auto source = std::find_if(
        data.sessions.begin(),
        data.sessions.end(),
        [&source_name](
            const SessionRecord &candidate)
        {
            return candidate.name == source_name;
        });

    if (source == data.sessions.end())
        return false;

    const auto conflict = std::find_if(
        data.sessions.begin(),
        data.sessions.end(),
        [&source_name,
         &target_name](
            const SessionRecord &candidate)
        {
            return candidate.name == target_name &&
                   candidate.name != source_name;
        });

    if (conflict != data.sessions.end())
        return false;

    SessionRecord stored = session;
    stored.name = target_name;
    *source = std::move(stored);

    const auto old_marker =
        std::string(SESSION_SECTION_PREFIX) +
        source_name;
    const auto new_marker =
        std::string(SESSION_SECTION_PREFIX) +
        target_name;

    for (auto &item : data.dock_order)
    {
        if (item == old_marker)
            item = new_marker;
    }

    return write_data(data);
}

bool LauncherManager::reorder_sessions(
    const std::vector<std::string> &names)
{
    StoredData data = read_data();

    std::vector<SessionRecord> ordered;
    ordered.reserve(data.sessions.size());

    for (const auto &name : names)
    {
        const auto trimmed_name = trimmed(name);

        const auto stored = std::find_if(
            data.sessions.begin(),
            data.sessions.end(),
            [&trimmed_name](
                const SessionRecord &candidate)
            {
                return candidate.name ==
                       trimmed_name;
            });

        if (stored == data.sessions.end())
            continue;

        const bool already_ordered =
            std::any_of(
                ordered.begin(),
                ordered.end(),
                [&trimmed_name](
                    const SessionRecord &candidate)
                {
                    return candidate.name ==
                           trimmed_name;
                });

        if (already_ordered)
            continue;

        ordered.push_back(*stored);
    }

    // A Session the caller did not mention keeps its place at the end rather
    // than being dropped from the store.
    for (const auto &session : data.sessions)
    {
        const bool already_ordered =
            std::any_of(
                ordered.begin(),
                ordered.end(),
                [&session](
                    const SessionRecord &candidate)
                {
                    return candidate.name ==
                           session.name;
                });

        if (!already_ordered)
            ordered.push_back(session);
    }

    if (ordered.size() == data.sessions.size() &&
        std::equal(
            ordered.begin(),
            ordered.end(),
            data.sessions.begin(),
            [](const SessionRecord &left,
               const SessionRecord &right)
            {
                return left.name == right.name;
            }))
    {
        return true;
    }

    data.sessions = std::move(ordered);

    // Reorder the Session marker slots as well. Definition blocks remain at
    // the bottom, but every public Session order follows the visual markers.
    std::vector<std::string> session_ids;
    session_ids.reserve(data.sessions.size());
    for (const auto &session : data.sessions)
    {
        session_ids.push_back(
            std::string(SESSION_SECTION_PREFIX) +
            session.name);
    }

    auto next_session_id = session_ids.begin();
    for (auto &desktop_id : data.dock_order)
    {
        if (desktop_id.compare(
                0,
                std::char_traits<char>::length(
                    SESSION_SECTION_PREFIX),
                SESSION_SECTION_PREFIX) == 0 &&
            next_session_id != session_ids.end())
        {
            desktop_id = *next_session_id++;
        }
    }

    data.dock_order.insert(
        data.dock_order.end(),
        next_session_id,
        session_ids.end());
    return write_data(data);
}

bool LauncherManager::remove_session(
    const std::string &name)
{
    StoredData data = read_data();
    const auto trimmed_name = trimmed(name);

    const auto removed = std::remove_if(
        data.sessions.begin(),
        data.sessions.end(),
        [&trimmed_name](
            const SessionRecord &candidate)
        {
            return candidate.name ==
                   trimmed_name;
        });

    if (removed == data.sessions.end())
        return false;

    data.sessions.erase(
        removed,
        data.sessions.end());

    return write_data(data);
}

// Persists the complete launcher order and every Session after validation by
// the caller. Rewriting one canonical file avoids partial state and keeps the
// file representation independent from GTK widget order.
bool LauncherManager::write_data(
    const StoredData &data) const
{
    const auto directory =
        Glib::path_get_dirname(
            m_data_path);

    if (g_mkdir_with_parents(
            directory.c_str(),
            0700) != 0)
    {
        g_warning(
            "Cannot create launcher directory '%s': %s",
            directory.c_str(),
            std::strerror(errno));
        return false;
    }

    const auto temporary_path =
        m_data_path + ".tmp";

    {
        std::ofstream file(
            temporary_path,
            std::ios::trunc);

        if (!file)
        {
            g_warning(
                "Cannot write launcher file '%s'",
                temporary_path.c_str());
            return false;
        }

        std::vector<std::string> remaining =
            data.desktop_ids;
        for (const auto &session : data.sessions)
        {
            remaining.push_back(
                std::string(SESSION_SECTION_PREFIX) +
                session.name);
        }

        const auto identity =
            [this](const std::string &value)
        {
            if (value.compare(
                    0,
                    std::char_traits<char>::length(
                        SESSION_SECTION_PREFIX),
                    SESSION_SECTION_PREFIX) == 0)
            {
                return value;
            }

            return normalize_resolved_id(value);
        };

        std::vector<std::string> order;
        order.reserve(remaining.size());

        for (const auto &stored_id : data.dock_order)
        {
            const auto stored_identity =
                identity(stored_id);
            const auto match = std::find_if(
                remaining.begin(),
                remaining.end(),
                [&identity, &stored_identity](
                    const std::string &candidate)
                {
                    return identity(candidate) ==
                           stored_identity;
                });

            if (match == remaining.end())
                continue;

            order.push_back(*match);
            remaining.erase(match);
        }

        order.insert(
            order.end(),
            remaining.begin(),
            remaining.end());

        // The leading block is the exact visual sequence. Session entries are
        // references only; their complete definitions are emitted below.
        for (const auto &desktop_id : order)
        {
            if (desktop_id.compare(
                    0,
                    std::char_traits<char>::length(
                        SESSION_SECTION_PREFIX),
                    SESSION_SECTION_PREFIX) == 0)
            {
                file << '['
                     << single_line(desktop_id)
                     << "]\n";
            }
            else
            {
                file << desktop_id << '\n';
            }
        }

        // Definitions always remain together at the bottom, independently of
        // where their lightweight markers appear in the visual sequence.
        for (const auto &session :
             data.sessions)
        {
            if (trimmed(session.name).empty())
                continue;

            file << '\n'
                 << '['
                 << SESSION_SECTION_PREFIX
                 << single_line(session.name)
                 << "]\n";

            // Always emit the key, even when empty. It makes this bottom
            // block unambiguously a definition rather than a leading marker.
            file << "icon="
                 << single_line(session.icon)
                 << '\n';

            for (const auto &item :
                 session.items)
            {
                // Never write an item that names no application; it would be
                // read back as a card corresponding to nothing.
                if (trimmed(item.desktop_file)
                        .empty())
                {
                    continue;
                }

                file << '['
                     << SESSION_ITEM_SECTION
                     << "]\n";
                write_session_value(
                    file,
                    "desktop-file",
                    item.desktop_file);
                write_session_value(
                    file,
                    "title",
                    item.title);
                write_session_value(
                    file,
                    "parameters",
                    item.parameters);
                write_session_value(
                    file,
                    "workspace",
                    item.workspace);
                write_session_value(
                    file,
                    "dimensions",
                    item.dimensions);
                write_session_value(
                    file,
                    "position",
                    item.position);
            }
        }

        if (!file)
        {
            g_warning(
                "Cannot finish launcher file '%s'",
                temporary_path.c_str());
            g_unlink(
                temporary_path.c_str());
            return false;
        }
    }

    if (g_rename(
            temporary_path.c_str(),
            m_data_path.c_str()) != 0)
    {
        g_warning(
            "Cannot replace launcher file '%s': %s",
            m_data_path.c_str(),
            std::strerror(errno));
        g_unlink(
            temporary_path.c_str());
        return false;
    }

    return true;
}
