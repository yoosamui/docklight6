#pragma once

#include <gdkmm/display.h>
#include <gdkmm/monitor.h>
#include <giomm/filemonitor.h>
#include <sigc++/connection.h>
#include <sigc++/signal.h>

#include <optional>
#include <string>
#include <vector>

struct DockMonitorInfo
{
    Glib::RefPtr<Gdk::Monitor> monitor;
    std::string identifier;
    bool primary = false;
    int width = 0;
    int height = 0;
    int scale = 1;
};

class DockMonitorManager
{
public:
    explicit DockMonitorManager(
        const std::string &requested_monitor =
            "primary");

    std::vector<DockMonitorInfo>
    available_monitors() const;

    void print_available_monitors() const;

    void set_requested_monitor(
        const std::string &identifier);

    Glib::RefPtr<Gdk::Monitor>
    selected_monitor() const;

    void start_monitoring();

    sigc::signal<
        void,
        const Glib::RefPtr<Gdk::Monitor> &> &
    signal_monitor_changed();

private:
    struct MonitorSnapshot
    {
        GdkMonitor *monitor = nullptr;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        int workarea_x = 0;
        int workarea_y = 0;
        int workarea_width = 0;
        int workarea_height = 0;
        int scale = 1;
    };

    Glib::RefPtr<Gdk::Monitor>
    resolve_requested_monitor(
        bool &used_fallback) const;

    MonitorSnapshot snapshot_for(
        const Glib::RefPtr<Gdk::Monitor> &monitor) const;

    void schedule_monitor_update();
    bool sample_monitor_state();
    void apply_monitor(
        const Glib::RefPtr<Gdk::Monitor> &monitor,
        const MonitorSnapshot &snapshot,
        bool used_fallback);
    void log_monitor(
        const Glib::RefPtr<Gdk::Monitor> &monitor,
        const MonitorSnapshot &snapshot) const;
    void connect_selected_monitor_signals();

    static bool same_snapshot(
        const MonitorSnapshot &left,
        const MonitorSnapshot &right);

private:
    Glib::RefPtr<Gdk::Display> m_display;
    Glib::RefPtr<Gdk::Monitor> m_selected_monitor;
    std::string m_requested_monitor = "primary";
    std::string m_warned_missing_monitor;

    std::optional<MonitorSnapshot> m_last_sample;
    std::optional<MonitorSnapshot> m_applied_snapshot;
    int m_stable_samples = 0;
    int m_sample_attempts = 0;
    bool m_monitoring = false;

    sigc::connection m_monitor_timer;
    sigc::connection m_monitor_added;
    sigc::connection m_monitor_removed;
    sigc::connection m_monitors_changed;
    sigc::connection m_geometry_changed;
    sigc::connection m_workarea_changed;
    sigc::connection m_scale_changed;
    Glib::RefPtr<Gio::FileMonitor>
        m_kde_output_monitor;

    sigc::signal<
        void,
        const Glib::RefPtr<Gdk::Monitor> &>
        m_signal_monitor_changed;
};
