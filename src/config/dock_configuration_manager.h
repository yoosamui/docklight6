// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_configuration_manager.h
//
// Purpose:
// Declares loading, validation, persistence, and monitoring of the
// per-user Docklight configuration.
//
// Responsibilities:
// - Ensure the configuration file and supported settings exist.
// - Convert text values into a validated DockConfiguration snapshot.
// - Coalesce filesystem events and notify consumers after changes.
//
// Dependencies and ownership:
// The manager owns its Gio file monitor, reload timer, current
// snapshot, and change signal. Signal subscribers own their state.
//
// Design notes:
// Consumers receive a complete snapshot, preventing UI components
// from interpreting configuration text independently.
//
// ------------------------------------------------------------

#pragma once

#include "dock_configuration.h"

#include <giomm/filemonitor.h>
#include <sigc++/connection.h>
#include <sigc++/signal.h>

#include <string>

class DockConfigurationManager
{
public:
    explicit DockConfigurationManager(
        const std::string &config_home = {});

    void start_monitoring();
    bool save_setting(
        const std::string &key,
        const std::string &value);

    const DockConfiguration &current() const;
    const std::string &config_path() const;

    sigc::signal<
        void,
        const DockConfiguration &> &
    signal_changed();

private:
    void ensure_config_file();
    void migrate_configuration();
    void ensure_setting(
        const char *key,
        const char *setting_template);
    void reload();
    void schedule_reload();
    void on_directory_changed(
        const Glib::RefPtr<Gio::File> &file,
        const Glib::RefPtr<Gio::File> &other_file,
        Gio::FileMonitorEvent event);

    bool is_config_file(
        const Glib::RefPtr<Gio::File> &file) const;

private:
    Glib::RefPtr<Gio::FileMonitor> m_monitor;

    DockConfiguration m_current;

    std::string m_config_directory;
    std::string m_config_path;

    sigc::connection m_reload_timer;

    sigc::signal<
        void,
        const DockConfiguration &>
        m_signal_changed;
};
