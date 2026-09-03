// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_application_controller.h
//
// Purpose:
// Declares application-level window actions used by a dock item.
//
// Responsibilities:
// - Resolve one launcher identity to its managed window group.
// - Apply minimize, activate, maximize, close, and cycling policy.
// - Present window entries without exposing backend details to the UI.
//
// Dependencies and ownership:
// The controller borrows WindowRegistry; it does not own backend
// windows. Cycle state and launcher identifiers are stored by value.
//
// Design notes:
// Group-action policy is centralized here so DockItem remains focused
// on GTK interaction and presentation.
//
// ------------------------------------------------------------

#pragma once

#include "windowing/managed_window.h"
#include "windowing/running_application.h"
#include "windowing/window_icon_geometry.h"

#include <cstddef>
#include <functional>
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
    // The window's own application identity. An item that stands for several
    // applications needs it to show each window's real icon.
    std::string desktop_file_name;
    std::vector<unsigned int> desktop_numbers;
    WindowGeometry frame_geometry;
    std::int64_t process_id = 0;

    bool active = false;
    bool minimized = false;
    bool application_auxiliary = false;
    bool on_current_desktop = true;
};

class DockApplicationController
{
public:
    DockApplicationController(
        WindowRegistry *registry,
        std::vector<std::string>
            application_identifiers);

    void set_application_identifiers(
        std::vector<std::string>
            application_identifiers);
    void set_window_filter(
        std::function<bool(const ManagedWindow &)>
            window_filter);

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
    bool close_window(
        const WindowId &window_id);
    bool toggle_window(
        const WindowId &window_id);
    bool show_window(
        const WindowId &window_id);
    bool set_icon_geometry(
        const WindowIconGeometry &geometry);
    void set_manage_all_workspaces(bool enabled);

    std::size_t window_count() const;

    std::vector<ApplicationWindowEntry>
    window_entries() const;

    void reset_window_cycle();

private:
    std::vector<WindowId>
    current_desktop_windows(
        const RunningApplication
            &application) const;
    std::vector<WindowId>
    most_recent_desktop_group(
        const RunningApplication
            &application,
        bool require_unminimized) const;
    bool activate_windows(
        const std::vector<WindowId>
            &window_ids);
    bool restore_all_and_activate(
        const RunningApplication
            &application,
        const std::vector<WindowId>
            &current_window_ids);
    bool minimize_windows(
        const std::vector<WindowId>
            &window_ids);
    bool group_is_frontmost(
        const RunningApplication
            &application) const;
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

    std::function<bool(const ManagedWindow &)>
        m_window_filter;

    // Storage for the merged view returned when an item stands for several
    // applications. Mutable because application() is a const query.
    mutable RunningApplication
        m_merged_application;

    WindowRegistry *m_registry = nullptr;
    bool m_manage_all_workspaces = true;
    bool m_group_hidden_by_toggle = false;
};
