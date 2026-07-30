#ifndef LAUNCHER_MANAGER_H
#define LAUNCHER_MANAGER_H

#include <giomm.h>

#include <string>
#include <vector>

class LauncherManager
{
public:
    explicit LauncherManager(
        std::string data_path = {});

    std::vector<std::string>
    attached_ids() const;

    Glib::RefPtr<Gio::AppInfo>
    find_application(
        const std::string &desktop_id) const;

    bool set_attached(
        const std::string &desktop_id,
        bool attached);
    bool is_attached(
        const std::string &desktop_id) const;

    static std::string
    normalize_desktop_id(
        const std::string &desktop_id);

private:
    std::vector<std::string>
    read_config() const;
    bool write_config(
        const std::vector<std::string>
            &desktop_ids) const;

private:
    std::string m_data_path;
};

#endif
