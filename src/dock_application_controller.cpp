#include "dock_application_controller.h"

#include "running_application.h"
#include "window_registry.h"

#include <utility>
#include <vector>

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
