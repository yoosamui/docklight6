#include "launcher_manager.h"

#include <gio/gdesktopappinfo.h>
#include <glib.h>
#include <glib/gstdio.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <utility>

namespace
{

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

    const auto apps =
        Gio::AppInfo::get_all();

    for (const auto &app : apps)
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
            matches(app->get_display_name()))
        {
            return app;
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

bool LauncherManager::set_attached(
    const std::string &desktop_id,
    bool attached)
{
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

std::vector<std::string>
LauncherManager::read_config() const
{
    std::vector<std::string> ids;

    std::ifstream file(m_data_path);

    std::string line;

    while (std::getline(file, line))
    {
        line = trimmed(line);

        if (line.empty() || line[0] == '#')
            continue;

        const auto normalized_id =
            normalize_resolved_id(line);

        const bool duplicate =
            std::any_of(
                ids.begin(),
                ids.end(),
                [this, &normalized_id](
                    const std::string
                        &candidate)
                {
                    return normalize_resolved_id(
                               candidate) ==
                           normalized_id;
                });

        if (!duplicate)
            ids.push_back(line);
    }

    return ids;
}

bool LauncherManager::write_config(
    const std::vector<std::string>
        &desktop_ids) const
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

        for (const auto &desktop_id :
             desktop_ids)
        {
            file << desktop_id << '\n';
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
