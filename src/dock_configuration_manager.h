#pragma once

#include "dock_configuration.h"

#include <giomm/filemonitor.h>
#include <sigc++/connection.h>
#include <sigc++/signal.h>

#include <string>

class DockConfigurationManager
{
public:
    DockConfigurationManager();

    const DockConfiguration &current() const;
    const std::string &config_path() const;

    sigc::signal<
        void,
        const DockConfiguration &> &
    signal_changed();

    void start_monitoring();

private:
    void ensure_config_file();
    void ensure_setting(
        const char *key,
        const char *setting_template);
    void reload();
    void schedule_reload();
    bool is_config_file(
        const Glib::RefPtr<Gio::File> &file) const;
    void on_directory_changed(
        const Glib::RefPtr<Gio::File> &file,
        const Glib::RefPtr<Gio::File> &other_file,
        Gio::FileMonitorEvent event);

private:
    DockConfiguration m_current;
    std::string m_config_directory;
    std::string m_config_path;

    Glib::RefPtr<Gio::FileMonitor> m_monitor;
    sigc::connection m_reload_timer;
    sigc::signal<
        void,
        const DockConfiguration &>
        m_signal_changed;
};
