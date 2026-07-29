#pragma once

#include "managed_window.h"
#include "window_icon_geometry.h"

#include <optional>
#include <string>
#include <vector>

class RunningApplication;
class WindowRegistry;

enum class WindowCycleDirection
{
    previous,
    next
};

struct ApplicationWindowEntry
{
    WindowId id;

    std::string caption;
    std::string icon_name;
    std::vector<unsigned int> desktop_numbers;

    bool active = false;
    bool minimized = false;
    bool on_current_desktop = true;
};

class DockApplicationController
{
public:
    DockApplicationController(
        WindowRegistry *registry,
        std::vector<std::string>
            application_identifiers);

    bool running() const;
    bool can_minimize() const;
    bool can_unminimize() const;
    bool can_maximize() const;
    bool can_close() const;
    bool toggle_minimized();
    bool minimize();
    bool unminimize();
    bool maximize();
    bool close_all();
    bool cycle_window(
        WindowCycleDirection direction);
    bool minimize_window(
        const WindowId &window_id);
    bool show_window(
        const WindowId &window_id);
    bool set_icon_geometry(
        const WindowIconGeometry &geometry);

    std::vector<ApplicationWindowEntry>
    window_entries() const;

    void reset_window_cycle();

private:
    bool has_minimized_window() const;
    bool has_unminimized_window() const;

    const RunningApplication *
    application() const;

private:
    std::optional<std::string>
        m_cycle_window_id;

    std::vector<std::string>
        m_application_identifiers;
    std::vector<std::string>
        m_cycle_window_ids;

    WindowRegistry *m_registry = nullptr;
};
