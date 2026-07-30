#include "dock_application_controller.h"

#include "running_application.h"
#include "window_registry.h"

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

namespace
{

bool contains_same_windows(
    const std::vector<WindowId> &left,
    const std::vector<WindowId> &right)
{
    return left.size() == right.size() &&
           std::is_permutation(
               left.begin(),
               left.end(),
               right.begin());
}

bool belongs_to_activation_desktop(
    const ManagedWindow &window,
    const ManagedWindow &target)
{
    // KWin switches to the target window's first desktop during activation.
    // Include only windows that will be visible on that same desktop.
    if (!target.desktop_ids.empty())
    {
        return std::find(
                   window.desktop_ids.begin(),
                   window.desktop_ids.end(),
                   target.desktop_ids.front()) !=
               window.desktop_ids.end();
    }

    if (!target.desktop_numbers.empty())
    {
        return std::find(
                   window.desktop_numbers.begin(),
                   window.desktop_numbers.end(),
                   target.desktop_numbers
                       .front()) !=
               window.desktop_numbers.end();
    }

    return window.id == target.id;
}

}

DockApplicationController::
    DockApplicationController(
        WindowRegistry *registry,
        std::vector<std::string>
            application_identifiers)
    : m_application_identifiers(
          std::move(
              application_identifiers)),
      m_registry(registry)
{
}

bool DockApplicationController::running() const
{
    const auto running_application =
        application();

    return running_application &&
           !running_application
                ->window_ids.empty();
}

bool DockApplicationController::
    can_minimize() const
{
    return has_unminimized_window() &&
           m_registry
               ->capabilities()
               .can_minimize;
}

bool DockApplicationController::
    can_unminimize() const
{
    return has_minimized_window() &&
           m_registry
               ->capabilities()
               .can_minimize;
}

bool DockApplicationController::
    can_maximize() const
{
    return running() &&
           m_registry
               ->capabilities()
               .can_maximize;
}

bool DockApplicationController::can_close() const
{
    return running() &&
           m_registry
               ->capabilities()
               .can_close;
}

bool DockApplicationController::
    toggle_minimized()
{
    const auto running_application =
        application();

    if (!running_application ||
        running_application
            ->window_ids.empty())
    {
        return false;
    }

    const auto current_windows =
        current_desktop_windows(
            *running_application);

    if (!m_manage_all_workspaces)
    {
        if (current_windows.empty())
            return false;

        if (running_application->active_window_id)
        {
            const auto active_window =
                m_registry->find_window(
                    *running_application
                         ->active_window_id);

            if (active_window &&
                !active_window->minimized &&
                active_window
                    ->on_current_desktop)
            {
                return minimize_windows(
                    current_windows);
            }
        }

        if (group_is_frontmost(
                *running_application))
        {
            return minimize_windows(
                current_windows);
        }

        return activate_windows(
            current_windows);
    }

    // An active application is already presented to the user. Hide its
    // complete group without raising or activating another window; this must
    // not switch desktops even when the group spans several of them.
    if (running_application->active_window_id)
    {
        const auto active_window =
            m_registry->find_window(
                *running_application
                     ->active_window_id);

        if (active_window &&
            !active_window->minimized &&
            active_window
                ->on_current_desktop)
        {
            return minimize();
        }
    }

    // KWin can clear activeWindow while the layer-shell dock handles the
    // click. Use the authoritative stacking order as a fallback: if the
    // top visible window on this desktop belongs to this group, the group is
    // already presented and the click should hide it. If another app is on
    // top, continue below and activate this group instead.
    if (group_is_frontmost(
            *running_application))
    {
        return minimize();
    }

    // A group on the current desktop always has priority, including when its
    // windows are minimized. If the preceding click hid the complete
    // application, restore every workspace's windows in place, then raise
    // and activate only the current desktop's group.
    if (!current_windows.empty())
    {
        if (!has_unminimized_window())
        {
            return restore_all_and_activate(
                *running_application,
                current_windows);
        }

        return activate_windows(
            current_windows);
    }

    auto target_windows =
        most_recent_desktop_group(
            *running_application,
            true);

    if (target_windows.empty())
    {
        target_windows =
            most_recent_desktop_group(
                *running_application,
                false);
    }

    return activate_windows(
        target_windows);
}

std::vector<WindowId>
DockApplicationController::
    current_desktop_windows(
        const RunningApplication
            &running_application) const
{
    std::vector<WindowId> window_ids;

    for (const auto &window_id :
         running_application.window_ids)
    {
        const auto window =
            m_registry->find_window(
                window_id);

        if (window &&
            window->on_current_desktop)
        {
            window_ids.push_back(
                window_id);
        }
    }

    return window_ids;
}

std::vector<WindowId>
DockApplicationController::
    most_recent_desktop_group(
        const RunningApplication
            &running_application,
        bool require_unminimized) const
{
    const ManagedWindow *target = nullptr;

    for (auto window_id =
             running_application
                 .window_ids.rbegin();
         window_id !=
             running_application
                 .window_ids.rend();
         ++window_id)
    {
        const auto window =
            m_registry->find_window(
                *window_id);

        if (!window ||
            window->on_current_desktop ||
            (require_unminimized &&
             window->minimized))
        {
            continue;
        }

        target = window;
        break;
    }

    if (!target)
        return {};

    std::vector<WindowId> window_ids;

    for (const auto &window_id :
         running_application.window_ids)
    {
        const auto window =
            m_registry->find_window(
                window_id);

        if (window &&
            !window->on_current_desktop &&
            belongs_to_activation_desktop(
                *window,
                *target))
        {
            window_ids.push_back(
                window_id);
        }
    }

    return window_ids;
}

bool DockApplicationController::activate_windows(
    const std::vector<WindowId>
        &window_ids)
{
    if (window_ids.empty())
        return false;

    return m_registry->present_windows(
        window_ids);
}

bool DockApplicationController::
    restore_all_and_activate(
        const RunningApplication
            &running_application,
        const std::vector<WindowId>
            &current_window_ids)
{
    if (current_window_ids.empty())
        return false;

    const auto all_window_ids =
        running_application.window_ids;

    bool accepted =
        m_registry->present_windows(
            current_window_ids);

    for (const auto &window_id :
         all_window_ids)
    {
        if (std::find(
                current_window_ids.begin(),
                current_window_ids.end(),
                window_id) !=
            current_window_ids.end())
        {
            continue;
        }

        const auto window =
            m_registry->find_window(
                window_id);

        if (window &&
            window->minimized)
        {
            accepted =
                m_registry
                    ->set_window_minimized(
                        window_id,
                        false) &&
                accepted;
        }
    }

    return accepted;
}

bool DockApplicationController::minimize_windows(
    const std::vector<WindowId>
        &window_ids)
{
    std::vector<WindowId>
        visible_window_ids;
    visible_window_ids.reserve(
        window_ids.size());

    for (const auto &window_id :
         window_ids)
    {
        const auto window =
            m_registry->find_window(
                window_id);

        if (!window ||
            window->minimized)
        {
            continue;
        }

        visible_window_ids.push_back(
            window_id);
    }

    return m_registry->hide_windows(
        visible_window_ids);
}

bool DockApplicationController::
    group_is_frontmost(
        const RunningApplication
            &running_application) const
{
    for (auto window =
             m_registry->windows().rbegin();
         window !=
             m_registry->windows().rend();
         ++window)
    {
        if (window->minimized ||
            !window->on_current_desktop)
        {
            continue;
        }

        return std::find(
                   running_application
                       .window_ids.begin(),
                   running_application
                       .window_ids.end(),
                   window->id) !=
               running_application
                   .window_ids.end();
    }

    return false;
}

bool DockApplicationController::minimize()
{
    const auto running_application =
        application();

    if (!running_application)
        return false;

    const auto window_ids =
        running_application->window_ids;

    return minimize_windows(
        window_ids);
}

bool DockApplicationController::unminimize()
{
    const auto running_application =
        application();

    if (!running_application)
        return false;

    const auto window_ids =
        running_application->window_ids;

    bool accepted = true;
    bool dispatched = false;

    for (const auto &window_id :
         window_ids)
    {
        const auto window =
            m_registry->find_window(
                window_id);

        if (!window ||
            !window->minimized)
        {
            continue;
        }

        dispatched = true;
        accepted =
            m_registry
                ->set_window_minimized(
                    window_id,
                    false) &&
            accepted;
    }

    return dispatched && accepted;
}

bool DockApplicationController::maximize()
{
    const auto running_application =
        application();

    if (!running_application ||
        running_application
            ->window_ids.empty())
    {
        return false;
    }

    const auto window_id =
        running_application
            ->active_window_id
            .value_or(
                running_application
                    ->window_ids.back());

    const auto window =
        m_registry->find_window(
            window_id);

    bool accepted = true;

    if (window &&
        window->minimized)
    {
        accepted =
            m_registry
                ->set_window_minimized(
                    window_id,
                    false) &&
            accepted;
    }

    accepted =
        m_registry->set_window_maximized(
            window_id,
            true) &&
        accepted;

    return accepted;
}

bool DockApplicationController::close_all()
{
    const auto running_application =
        application();

    if (!running_application)
        return false;

    const auto window_ids =
        running_application->window_ids;

    bool accepted = !window_ids.empty();

    for (const auto &window_id :
         window_ids)
    {
        accepted =
            m_registry->close_window(
                window_id) &&
            accepted;
    }

    return accepted;
}

bool DockApplicationController::cycle_window(
    WindowCycleDirection direction)
{
    const auto running_application =
        application();

    if (!running_application ||
        running_application
            ->window_ids.empty())
    {
        reset_window_cycle();
        return false;
    }

    const auto available_window_ids =
        m_manage_all_workspaces
            ? running_application->window_ids
            : current_desktop_windows(
                  *running_application);

    if (available_window_ids.empty())
    {
        reset_window_cycle();
        return false;
    }

    if (!contains_same_windows(
            m_cycle_window_ids,
            available_window_ids))
    {
        reset_window_cycle();

        m_cycle_window_ids =
            available_window_ids;
    }

    auto current =
        m_cycle_window_ids.end();

    if (m_cycle_window_id)
    {
        current =
            std::find(
                m_cycle_window_ids.begin(),
                m_cycle_window_ids.end(),
                *m_cycle_window_id);
    }

    if (current ==
            m_cycle_window_ids.end() &&
        running_application
            ->active_window_id)
    {
        current =
            std::find(
                m_cycle_window_ids.begin(),
                m_cycle_window_ids.end(),
                *running_application
                     ->active_window_id);
    }

    auto target =
        m_cycle_window_ids.begin();

    if (current !=
        m_cycle_window_ids.end())
    {
        if (direction ==
            WindowCycleDirection::next)
        {
            target = std::next(current);

            if (target ==
                m_cycle_window_ids.end())
            {
                target =
                    m_cycle_window_ids.begin();
            }
        }
        else if (current ==
                 m_cycle_window_ids.begin())
        {
            target =
                std::prev(
                    m_cycle_window_ids.end());
        }
        else
        {
            target =
                std::prev(current);
        }
    }
    else if (direction ==
             WindowCycleDirection::previous)
    {
        target =
            std::prev(
                m_cycle_window_ids.end());
    }

    const auto window_id = *target;

    m_cycle_window_id =
        window_id;

    return show_window(window_id);
}

void DockApplicationController::
    set_manage_all_workspaces(
        bool enabled)
{
    if (m_manage_all_workspaces == enabled)
        return;

    m_manage_all_workspaces = enabled;
    reset_window_cycle();
}

bool DockApplicationController::show_window(
    const WindowId &window_id)
{
    const auto running_application =
        application();

    if (!running_application ||
        std::find(
            running_application
                ->window_ids.begin(),
            running_application
                ->window_ids.end(),
            window_id) ==
            running_application
                ->window_ids.end())
    {
        return false;
    }

    const auto window =
        m_registry->find_window(
            window_id);

    if (!window)
        return false;

    bool accepted = true;

    if (window->minimized)
    {
        accepted =
            m_registry
                ->set_window_minimized(
                    window_id,
                    false) &&
            accepted;
    }

    accepted =
        m_registry->raise_window(
            window_id) &&
        accepted;

    accepted =
        m_registry->activate_window(
            window_id) &&
        accepted;

    return accepted;
}

bool DockApplicationController::
    minimize_window(
        const WindowId &window_id)
{
    const auto running_application =
        application();

    if (!running_application ||
        std::find(
            running_application
                ->window_ids.begin(),
            running_application
                ->window_ids.end(),
            window_id) ==
            running_application
                ->window_ids.end())
    {
        return false;
    }

    const auto window =
        m_registry->find_window(
            window_id);

    if (!window)
        return false;

    if (window->minimized)
        return true;

    return m_registry
        ->set_window_minimized(
            window_id,
            true);
}

std::vector<ApplicationWindowEntry>
DockApplicationController::
    window_entries() const
{
    std::vector<ApplicationWindowEntry>
        entries;

    const auto running_application =
        application();

    if (!running_application)
        return entries;

    entries.reserve(
        running_application
            ->window_ids.size());

    for (const auto &window_id :
         running_application
             ->window_ids)
    {
        const auto window =
            m_registry->find_window(
                window_id);

        if (!window)
            continue;

        ApplicationWindowEntry entry;

        entry.id = window_id;
        entry.caption =
            window->caption;
        entry.icon_name =
            window->icon_name;
        entry.desktop_numbers =
            window->desktop_numbers;
        entry.active =
            running_application
                ->active_window_id ==
            std::optional<WindowId>{
                window_id};
        entry.minimized =
            window->minimized;
        entry.on_current_desktop =
            window->on_current_desktop;

        entries.push_back(
            std::move(entry));
    }

    return entries;
}

std::size_t DockApplicationController::
    window_count() const
{
    const auto running_application =
        application();

    return running_application
               ? running_application
                     ->window_ids.size()
               : 0;
}

void DockApplicationController::
    reset_window_cycle()
{
    m_cycle_window_ids.clear();
    m_cycle_window_id.reset();
}

bool DockApplicationController::
    set_icon_geometry(
        const WindowIconGeometry &geometry)
{
    const auto running_application =
        application();

    if (!running_application ||
        !m_registry
             ->capabilities()
             .accepts_icon_geometry)
    {
        return false;
    }

    bool accepted = true;

    for (const auto &window_id :
         running_application->window_ids)
    {
        accepted =
            m_registry
                ->set_window_icon_geometry(
                    window_id,
                    geometry) &&
            accepted;
    }

    return accepted;
}

bool DockApplicationController::
    has_minimized_window() const
{
    const auto running_application =
        application();

    if (!running_application)
        return false;

    for (const auto &window_id :
         running_application->window_ids)
    {
        const auto window =
            m_registry->find_window(
                window_id);

        if (window &&
            window->minimized)
        {
            return true;
        }
    }

    return false;
}

bool DockApplicationController::
    has_unminimized_window() const
{
    const auto running_application =
        application();

    if (!running_application)
        return false;

    for (const auto &window_id :
         running_application->window_ids)
    {
        const auto window =
            m_registry->find_window(
                window_id);

        if (window &&
            !window->minimized)
        {
            return true;
        }
    }

    return false;
}

const RunningApplication *
DockApplicationController::application() const
{
    if (!m_registry ||
        !m_registry->connected())
    {
        return nullptr;
    }

    for (const auto &identifier :
         m_application_identifiers)
    {
        const auto running_application =
            m_registry->find_application(
                identifier);

        if (running_application)
            return running_application;
    }

    return nullptr;
}
