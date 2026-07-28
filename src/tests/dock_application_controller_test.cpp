#include "dock_application_controller.h"
#include "fake_window_backend.h"
#include "window_registry.h"

#include <cassert>
#include <optional>
#include <string>

namespace
{

ManagedWindow window(
    const WindowId &id,
    bool minimized = false,
    const std::string &desktop_file_name =
        "org.kde.dolphin")
{
    ManagedWindow window;

    window.id = id;
    window.desktop_file_name =
        desktop_file_name;
    window.minimized = minimized;

    return window;
}

void verifies_launcher_window_actions()
{
    FakeWindowBackend backend;

    backend.set_snapshot(
        {
            window("window-1"),
            window("window-2")
        },
        {
            "window-1",
            "window-2"
        },
        WindowId{"window-2"});

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {
            "org.kde.dolphin.desktop",
            "dolphin"
        });

    assert(controller.running());
    assert(controller.can_minimize());
    assert(!controller.can_unminimize());
    assert(controller.can_maximize());
    assert(controller.can_close());

    assert(controller
               .toggle_minimized());
    assert(registry
               .find_window("window-1")
               ->minimized);
    assert(registry
               .find_window("window-2")
               ->minimized);
    assert(!controller.can_minimize());
    assert(controller.can_unminimize());

    backend.set_active_window(
        std::nullopt);

    assert(controller
               .toggle_minimized());
    assert(!registry
                .find_window("window-1")
                ->minimized);
    assert(!registry
                .find_window("window-2")
                ->minimized);
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "window-2"});

    assert(controller.minimize());
    assert(registry
               .find_window("window-1")
               ->minimized);
    assert(registry
               .find_window("window-2")
               ->minimized);

    assert(controller.unminimize());
    assert(!registry
                .find_window("window-1")
                ->minimized);
    assert(!registry
                .find_window("window-2")
                ->minimized);

    assert(controller.minimize());

    assert(controller.maximize());
    assert(registry
               .find_window("window-2")
               ->maximized);
    assert(!registry
                .find_window("window-2")
                ->minimized);

    assert(controller.close_all());
    assert(!controller.running());
    assert(registry.windows().empty());
}

void verifies_missing_application()
{
    FakeWindowBackend backend;
    WindowRegistry registry(backend);

    registry.start();

    DockApplicationController controller(
        &registry,
        {
            "org.mozilla.firefox.desktop",
            "firefox"
        });

    assert(!controller.running());
    assert(!controller.can_minimize());
    assert(!controller.can_unminimize());
    assert(!controller.can_maximize());
    assert(!controller.can_close());
    assert(!controller
                .toggle_minimized());
    assert(!controller.minimize());
    assert(!controller.unminimize());
    assert(!controller.maximize());
    assert(!controller.close_all());
}

void verifies_alternate_application_identity()
{
    FakeWindowBackend backend;

    backend.set_snapshot(
        {
            window(
                "window-1",
                false,
                "org.gnome.SystemMonitor")
        },
        {
            "window-1"
        },
        WindowId{"window-1"});

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {
            "gnome-system-monitor-kde.desktop",
            "gnome-system-monitor",
            "org.gnome.SystemMonitor"
        });

    assert(controller.running());
    assert(controller.can_minimize());
}

}

int main()
{
    verifies_launcher_window_actions();
    verifies_missing_application();
    verifies_alternate_application_identity();

    return 0;
}
