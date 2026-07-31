// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// dock_monitor_manager.cpp
//
// Implementation overview:
// Implements monitor enumeration and selection across GDK and KDE
// output metadata, then observes the selected monitor for stable changes.
//
// Important implementation decisions:
// - User identifiers are normalized before matching output metadata.
// - A missing requested output falls back to the primary monitor.
// - Repeated samples prevent transient geometry from reaching layout code.
// - Monitor logs describe applied state rather than raw event traffic.
//
// ------------------------------------------------------------

#include "dock_monitor_manager.h"
#include "config.h"

#include <gdkmm/rectangle.h>
#include <gdkmm/screen.h>
#include <giomm/file.h>
#include <glib.h>
#include <glibmm/i18n.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>
#include <glibmm/spawn.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <limits>
#include <regex>
#include <sstream>

namespace
{

constexpr unsigned int MONITOR_SAMPLE_INTERVAL_MS = 300; // Delay between monitor samples
constexpr int REQUIRED_STABLE_SAMPLES = 2; // Matching samples required before applying
constexpr int MAX_SAMPLE_ATTEMPTS = 7; // Maximum attempts before accepting a sample

struct KdeOutput
{
    std::string name;
    int priority = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool has_geometry = false;
};

std::string trimmed(
    const Glib::ustring &input)
{
    std::string value = input.raw();

    auto first =
        std::find_if_not(
            value.begin(),
            value.end(),
            [](unsigned char character)
            {
                return std::isspace(character);
            });

    auto last =
        std::find_if_not(
            value.rbegin(),
            value.rend(),
            [](unsigned char character)
            {
                return std::isspace(character);
            })
            .base();

    if (first >= last)
        return {};

    return std::string(first, last);
}

std::string folded(
    const std::string &input)
{
    std::string result = input;

    std::transform(
        result.begin(),
        result.end(),
        result.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(
                std::tolower(character));
        });

    return result;
}

std::string base_identifier(
    const Glib::RefPtr<Gdk::Monitor> &monitor,
    int index)
{
    const auto manufacturer =
        trimmed(
            monitor->get_manufacturer());

    const auto model =
        trimmed(
            monitor->get_model());

    if (!manufacturer.empty() &&
        !model.empty())
    {
        return manufacturer + ":" + model;
    }

    if (!model.empty())
        return model;

    if (!manufacturer.empty())
        return manufacturer;

    return "monitor-" +
           std::to_string(index + 1);
}

std::vector<KdeOutput> kde_outputs()
{
    std::vector<KdeOutput> outputs;

    const auto executable =
        Glib::find_program_in_path(
            "kscreen-doctor");

    if (executable.empty())
        return outputs;

    std::string standard_output;
    int exit_status = 0;

    try
    {
        const std::vector<std::string> arguments{
            executable,
            "-o"};

        Glib::spawn_sync(
            {},
            arguments,
            Glib::SPAWN_DEFAULT,
            Glib::SlotSpawnChildSetup{},
            &standard_output,
            nullptr,
            &exit_status);
    }
    catch (const Glib::Error &)
    {
        return outputs;
    }

    if (exit_status != 0)
        return outputs;

    standard_output =
        std::regex_replace(
            standard_output,
            std::regex(
                "\x1B\\[[0-9;]*[A-Za-z]"),
            "");

    const std::regex output_pattern(
        R"(^Output:\s+[0-9]+\s+(\S+))");

    const std::regex priority_pattern(
        R"(^\s*priority\s+([0-9]+))");

    const std::regex geometry_pattern(
        R"(^\s*Geometry:\s+(-?[0-9]+),(-?[0-9]+)\s+([0-9]+)x([0-9]+))");

    std::istringstream lines(
        standard_output);

    std::string line;
    KdeOutput *current = nullptr;

    while (std::getline(lines, line))
    {
        std::smatch match;

        if (std::regex_search(
                line,
                match,
                output_pattern))
        {
            outputs.push_back({});
            current = &outputs.back();
            current->name = match[1].str();
            continue;
        }

        if (!current)
            continue;

        if (std::regex_search(
                line,
                match,
                priority_pattern))
        {
            current->priority =
                std::stoi(
                    match[1].str());
        }
        else if (std::regex_search(
                     line,
                     match,
                     geometry_pattern))
        {
            current->x =
                std::stoi(match[1].str());
            current->y =
                std::stoi(match[2].str());
            current->width =
                std::stoi(match[3].str());
            current->height =
                std::stoi(match[4].str());
            current->has_geometry = true;
        }
    }

    return outputs;
}

}

DockMonitorManager::DockMonitorManager(
    const std::string &requested_monitor)
    : m_display(
          Gdk::Display::get_default()),
      m_requested_monitor(
          requested_monitor.empty()
              ? "primary"
              : requested_monitor)
{
    bool used_fallback = false;

    m_selected_monitor =
        resolve_requested_monitor(
            used_fallback);

    if (m_selected_monitor)
        m_applied_snapshot =
            snapshot_for(
                m_selected_monitor);
}

std::vector<DockMonitorInfo>
DockMonitorManager::available_monitors() const
{
    std::vector<DockMonitorInfo> monitors;

    if (!m_display)
        return monitors;

    auto primary =
        m_display->get_primary_monitor();

    const auto native_outputs =
        kde_outputs();

    if (!native_outputs.empty())
        primary.reset();

    int primary_priority =
        std::numeric_limits<int>::max();

    if (!native_outputs.empty())
    {
        for (int index = 0;
             index < m_display->get_n_monitors();
             ++index)
        {
            auto monitor =
                m_display->get_monitor(index);

            if (!monitor)
                continue;

            Gdk::Rectangle geometry;
            monitor->get_geometry(geometry);

            for (const auto &output :
                 native_outputs)
            {
                if (output.has_geometry &&
                    output.priority > 0 &&
                    output.priority <
                        primary_priority &&
                    output.x ==
                        geometry.get_x() &&
                    output.y ==
                        geometry.get_y() &&
                    output.width ==
                        geometry.get_width() &&
                    output.height ==
                        geometry.get_height())
                {
                    primary = monitor;
                    primary_priority =
                        output.priority;
                }
            }
        }
    }

    if (!primary)
    {
        long long largest_area = -1;

        for (int index = 0;
             index < m_display->get_n_monitors();
             ++index)
        {
            auto monitor =
                m_display->get_monitor(index);

            if (!monitor)
                continue;

            Gdk::Rectangle geometry;
            monitor->get_geometry(geometry);

            const long long area =
                static_cast<long long>(
                    geometry.get_width()) *
                geometry.get_height();

            if (area > largest_area)
            {
                primary = monitor;
                largest_area = area;
            }
        }
    }

    std::vector<std::string> identifiers;

    for (int index = 0;
         index < m_display->get_n_monitors();
         ++index)
    {
        auto monitor =
            m_display->get_monitor(index);

        std::string identifier;

        if (monitor)
        {
            Gdk::Rectangle geometry;
            monitor->get_geometry(geometry);

            for (const auto &output :
                 native_outputs)
            {
                if (output.has_geometry &&
                    output.x ==
                        geometry.get_x() &&
                    output.y ==
                        geometry.get_y() &&
                    output.width ==
                        geometry.get_width() &&
                    output.height ==
                        geometry.get_height())
                {
                    identifier =
                        output.name;
                    break;
                }
            }

            if (identifier.empty())
            {
                identifier =
                    base_identifier(
                        monitor,
                        index);
            }
        }

        identifiers.push_back(identifier);
    }

    for (std::size_t index = 0;
         index < identifiers.size();
         ++index)
    {
        if (identifiers[index].empty())
            continue;

        int duplicate_number = 0;
        int duplicate_count = 0;

        for (std::size_t other = 0;
             other < identifiers.size();
             ++other)
        {
            if (folded(identifiers[other]) ==
                folded(identifiers[index]))
            {
                ++duplicate_count;

                if (other <= index)
                    ++duplicate_number;
            }
        }

        auto monitor =
            m_display->get_monitor(
                static_cast<int>(index));

        if (!monitor)
            continue;

        Gdk::Rectangle geometry;
        monitor->get_geometry(geometry);

        DockMonitorInfo info;
        info.monitor = monitor;
        info.identifier =
            identifiers[index];

        if (duplicate_count > 1)
        {
            info.identifier +=
                "@" +
                std::to_string(
                    duplicate_number);
        }

        info.primary =
            monitor == primary;
        info.width =
            geometry.get_width();
        info.height =
            geometry.get_height();
        info.scale =
            monitor->get_scale_factor();

        monitors.push_back(info);
    }

    return monitors;
}

void DockMonitorManager::print_available_monitors() const
{
    const auto monitors =
        available_monitors();

    if (monitors.empty())
    {
        std::cerr
            << _("No monitors are available.")
            << std::endl;
        return;
    }

    std::cout
        << _("Available monitors:")
        << std::endl;

    for (const auto &monitor : monitors)
    {
        std::cout
            << "  monitor = "
            << monitor.identifier
            << "  "
            << monitor.width
            << "x"
            << monitor.height;

        if (monitor.scale > 1)
            std::cout
                << " "
                << _("Scale")
                << "="
                << monitor.scale;

        if (monitor.primary)
            std::cout
                << "  ["
                << C_("monitor status", "Primary")
                << "]";

        std::cout
            << std::endl;
    }

    std::cout
        << "  monitor = primary"
        << "  ("
        << C_("monitor status", "default")
        << ")"
        << std::endl;
}

void DockMonitorManager::set_requested_monitor(
    const std::string &identifier)
{
    const auto requested =
        identifier.empty()
            ? std::string{"primary"}
            : identifier;

    if (folded(requested) ==
        folded(m_requested_monitor))
    {
        return;
    }

    m_requested_monitor = requested;
    m_warned_missing_monitor.clear();
    schedule_monitor_update();
}

Glib::RefPtr<Gdk::Monitor>
DockMonitorManager::selected_monitor() const
{
    return m_selected_monitor;
}

void DockMonitorManager::start_monitoring()
{
    if (m_monitoring ||
        !m_display)
    {
        return;
    }

    m_monitoring = true;

    m_monitor_added =
        m_display->signal_monitor_added().connect(
            [this](
                const Glib::RefPtr<Gdk::Monitor> &)
            {
                schedule_monitor_update();
            });

    m_monitor_removed =
        m_display->signal_monitor_removed().connect(
            [this](
                const Glib::RefPtr<Gdk::Monitor> &)
            {
                schedule_monitor_update();
            });

    auto screen =
        Gdk::Screen::get_default();

    if (screen)
    {
        m_monitors_changed =
            screen->signal_monitors_changed().connect(
                [this]()
                {
                    schedule_monitor_update();
                });
    }

    connect_selected_monitor_signals();

    if (m_selected_monitor &&
        m_applied_snapshot)
    {
        log_monitor(
            m_selected_monitor,
            *m_applied_snapshot);
    }

    try
    {
        const auto output_config_path =
            Glib::build_filename(
                Glib::get_user_config_dir(),
                "kwinoutputconfig.json");

        auto output_config =
            Gio::File::create_for_path(
                output_config_path);

        if (output_config->query_exists())
        {
            m_kde_output_monitor =
                output_config->monitor_file();

            m_kde_output_monitor
                ->signal_changed()
                .connect(
                    [this](
                        const Glib::RefPtr<Gio::File> &,
                        const Glib::RefPtr<Gio::File> &,
                        Gio::FileMonitorEvent)
                    {
                        schedule_monitor_update();
                    });
        }
    }
    catch (const Glib::Error &)
    {
        // KDE integration is optional. GTK monitor events remain active.
    }

    schedule_monitor_update();
}

sigc::signal<
    void,
    const Glib::RefPtr<Gdk::Monitor> &> &
DockMonitorManager::signal_monitor_changed()
{
    return m_signal_monitor_changed;
}

// Resolves the configured output identifier and reports whether primary
// monitor fallback was required. Matching is isolated here so all callers
// observe the same normalization and fallback policy.
Glib::RefPtr<Gdk::Monitor>
DockMonitorManager::resolve_requested_monitor(
    bool &used_fallback) const
{
    used_fallback = false;

    if (!m_display)
        return {};

    const auto monitors =
        available_monitors();

    if (folded(m_requested_monitor) !=
        "primary")
    {
        for (const auto &info : monitors)
        {
            if (folded(info.identifier) ==
                folded(m_requested_monitor))
            {
                return info.monitor;
            }
        }

        used_fallback = true;
    }

    for (const auto &info : monitors)
    {
        if (info.primary)
            return info.monitor;
    }

    Glib::RefPtr<Gdk::Monitor> largest;
    long long largest_area = -1;

    for (const auto &info : monitors)
    {
        const long long area =
            static_cast<long long>(
                info.width) *
            info.height;

        if (area > largest_area)
        {
            largest = info.monitor;
            largest_area = area;
        }
    }

    return largest;
}

DockMonitorManager::MonitorSnapshot
DockMonitorManager::snapshot_for(
    const Glib::RefPtr<Gdk::Monitor> &monitor) const
{
    MonitorSnapshot snapshot;

    if (!monitor)
        return snapshot;

    Gdk::Rectangle geometry;
    Gdk::Rectangle workarea;

    monitor->get_geometry(geometry);
    monitor->get_workarea(workarea);

    snapshot.monitor = monitor->gobj();
    snapshot.x = geometry.get_x();
    snapshot.y = geometry.get_y();
    snapshot.width = geometry.get_width();
    snapshot.height = geometry.get_height();
    snapshot.workarea_x = workarea.get_x();
    snapshot.workarea_y = workarea.get_y();
    snapshot.workarea_width = workarea.get_width();
    snapshot.workarea_height = workarea.get_height();
    snapshot.scale = monitor->get_scale_factor();

    return snapshot;
}

void DockMonitorManager::schedule_monitor_update()
{
    if (m_monitor_timer.connected())
        m_monitor_timer.disconnect();

    m_last_sample.reset();
    m_stable_samples = 0;
    m_sample_attempts = 0;

    m_monitor_timer =
        Glib::signal_timeout().connect(
            sigc::mem_fun(
                *this,
                &DockMonitorManager::
                    sample_monitor_state),
            MONITOR_SAMPLE_INTERVAL_MS);
}

bool DockMonitorManager::sample_monitor_state()
{
    bool used_fallback = false;

    auto monitor =
        resolve_requested_monitor(
            used_fallback);

    const auto snapshot =
        snapshot_for(monitor);

    ++m_sample_attempts;

    if (m_last_sample &&
        same_snapshot(
            *m_last_sample,
            snapshot))
    {
        ++m_stable_samples;
    }
    else
    {
        m_last_sample = snapshot;
        m_stable_samples = 1;
    }

    if (m_stable_samples <
            REQUIRED_STABLE_SAMPLES &&
        m_sample_attempts <
            MAX_SAMPLE_ATTEMPTS)
    {
        return true;
    }

    apply_monitor(
        monitor,
        snapshot,
        used_fallback);

    return false;
}

void DockMonitorManager::apply_monitor(
    const Glib::RefPtr<Gdk::Monitor> &monitor,
    const MonitorSnapshot &snapshot,
    bool used_fallback)
{
    if (!monitor)
    {
        g_warning(
            "No monitor is available for DockLight");
        return;
    }

    if (used_fallback &&
        m_warned_missing_monitor !=
            m_requested_monitor)
    {
        g_warning(
            "Monitor '%s' is unavailable; using the primary monitor",
            m_requested_monitor.c_str());

        m_warned_missing_monitor =
            m_requested_monitor;
    }
    else if (!used_fallback)
    {
        m_warned_missing_monitor.clear();
    }

    const bool changed =
        monitor != m_selected_monitor ||
        !m_applied_snapshot ||
        !same_snapshot(
            *m_applied_snapshot,
            snapshot);

    if (!changed)
        return;

    m_selected_monitor = monitor;
    m_applied_snapshot = snapshot;

    connect_selected_monitor_signals();

    log_monitor(
        m_selected_monitor,
        snapshot);

    m_signal_monitor_changed.emit(
        m_selected_monitor);
}

void DockMonitorManager::log_monitor(
    const Glib::RefPtr<Gdk::Monitor> &monitor,
    const MonitorSnapshot &snapshot) const
{
    std::string identifier =
        m_requested_monitor;

    for (const auto &available :
         available_monitors())
    {
        if (available.monitor == monitor)
        {
            identifier =
                available.identifier;
            break;
        }
    }

    g_message(
        "Current monitor: %s; output=%dx%d at %d,%d; "
        "workarea=%dx%d at %d,%d; scale=%d",
        identifier.c_str(),
        snapshot.width,
        snapshot.height,
        snapshot.x,
        snapshot.y,
        snapshot.workarea_width,
        snapshot.workarea_height,
        snapshot.workarea_x,
        snapshot.workarea_y,
        snapshot.scale);
}

void DockMonitorManager::
    connect_selected_monitor_signals()
{
    if (m_geometry_changed.connected())
        m_geometry_changed.disconnect();

    if (m_workarea_changed.connected())
        m_workarea_changed.disconnect();

    if (m_scale_changed.connected())
        m_scale_changed.disconnect();

    if (!m_selected_monitor)
        return;

    m_geometry_changed =
        m_selected_monitor
            ->property_geometry()
            .signal_changed()
            .connect(
                [this]()
                {
                    schedule_monitor_update();
                });

    m_workarea_changed =
        m_selected_monitor
            ->property_workarea()
            .signal_changed()
            .connect(
                [this]()
                {
                    schedule_monitor_update();
                });

    m_scale_changed =
        m_selected_monitor
            ->property_scale_factor()
            .signal_changed()
            .connect(
                [this]()
                {
                    schedule_monitor_update();
                });
}

bool DockMonitorManager::same_snapshot(
    const MonitorSnapshot &left,
    const MonitorSnapshot &right)
{
    return left.monitor == right.monitor &&
           left.x == right.x &&
           left.y == right.y &&
           left.width == right.width &&
           left.height == right.height &&
           left.workarea_x ==
               right.workarea_x &&
           left.workarea_y ==
               right.workarea_y &&
           left.workarea_width ==
               right.workarea_width &&
           left.workarea_height ==
               right.workarea_height &&
           left.scale == right.scale;
}
