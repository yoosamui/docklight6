#include "launcher_manager.h"
#include <iostream>
#include <fstream>

std::vector<Launcher> LauncherManager::load_applications()
{
    std::vector<Launcher> result;

    auto ids = read_config();
    g_message("Config Loaded");

    for (const auto &id : ids)
    {
        auto app = find_app(id);

        if (!app)
        {
            std::cout
                << "Missing: "
                << id
                << std::endl;

            continue;
        }

        Launcher launcher;
        launcher.app = app;

        result.push_back(launcher);
    }

    return result;
}

Glib::RefPtr<Gio::AppInfo> LauncherManager::find_app(const std::string &desktop_id)
{
    auto apps = Gio::AppInfo::get_all();

    for (const auto &app : apps)
    {
        if (!app)
            continue;

        if (app->get_id() == desktop_id)
            return app;
    }

    return {};
}
std::vector<std::string> LauncherManager::read_config()
{
    std::vector<std::string> ids;

    std::ifstream file(
        Glib::build_filename(
            Glib::get_home_dir(),
            ".config",
            "docklight6",
            "docklight.conf"));

    std::string line;

    while (std::getline(file, line))
    {

        if (line.empty() || line[0] == '#')
            continue;

        ids.push_back(line);
    }

    return ids;
}