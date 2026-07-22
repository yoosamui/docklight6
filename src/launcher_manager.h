#ifndef LAUNCHER_MANAGER_H
#define LAUNCHER_MANAGER_H

#include <giomm.h>
#include <vector>

struct Launcher
{
    Glib::RefPtr<Gio::AppInfo> app;
};

class LauncherManager
{
public:
    std::vector<Launcher> load_applications();

private:
    std::vector<std::string> read_config();
    Glib::RefPtr<Gio::AppInfo> find_app(const std::string &desktop_id);
};

#endif