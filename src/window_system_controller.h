#pragma once

#include <sigc++/connection.h>

#include <memory>

class KWinIntegrationService;
class KWinWindowBackend;
class WindowRegistry;

class WindowSystemController
{
public:
    WindowSystemController();
    ~WindowSystemController();

    void start();
    void stop();

    bool available() const;

    WindowRegistry *registry();
    const WindowRegistry *registry() const;

private:
    static bool is_kde_wayland_session();

    void on_connection_changed(
        bool connected);

private:
    std::unique_ptr<KWinWindowBackend>
        m_kwin_backend;
    std::unique_ptr<WindowRegistry>
        m_registry;
    std::unique_ptr<KWinIntegrationService>
        m_kwin_service;

    sigc::connection
        m_connection_changed;

    bool m_started = false;
};
