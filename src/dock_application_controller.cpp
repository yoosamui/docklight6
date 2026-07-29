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

    if (has_unminimized_window())
        return minimize();

    const auto window_id =
        running_application
            ->active_window_id
            .value_or(
                running_application
                    ->window_ids.back());

    bool accepted = unminimize();

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

bool DockApplicationController::minimize()
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
            window->minimized)
        {
            continue;
        }

        dispatched = true;
        accepted =
            m_registry
                ->set_window_minimized(
                    window_id,
                    true) &&
            accepted;
    }

    return dispatched && accepted;
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

    if (!contains_same_windows(
            m_cycle_window_ids,
            running_application
                ->window_ids))
    {
        reset_window_cycle();
        m_cycle_window_ids =
            running_application
                ->window_ids;
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
