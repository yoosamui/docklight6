#pragma once

#include <string>
#include <vector>

class RunningApplication;
class WindowRegistry;

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

private:
    bool has_minimized_window() const;
    bool has_unminimized_window() const;

    const RunningApplication *
    application() const;

private:
    std::vector<std::string>
        m_application_identifiers;

    WindowRegistry *m_registry = nullptr;
};
