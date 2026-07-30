#include "fake_window_backend.h"
#include "window_registry.h"

#include <cassert>
#include <optional>
#include <string>
#include <vector>

namespace
{

ManagedWindow window(
    const WindowId &id,
    const std::string &desktop_file_name,
    bool skip_taskbar = false)
{
    ManagedWindow window;

    window.id = id;
    window.desktop_file_name =
        desktop_file_name;
    window.skip_taskbar = skip_taskbar;

    return window;
}

void verifies_snapshot_and_grouping()
{
    FakeWindowBackend backend;

    backend.set_snapshot(
        {
            window(
                "window-1",
                "org.kde.dolphin"),
            window(
                "window-2",
                "org.kde.dolphin.desktop"),
            window(
                "window-3",
                "org.kde.plasmashell",
                true),
            window(
                "window-4",
                "")
        },
        {
            "window-2",
            "window-1",
            "window-4"
        },
        WindowId{"window-2"});

    WindowRegistry registry(backend);
    registry.start();

    assert(registry.connected());
    assert(registry.windows().size() == 3);
    assert(registry.windows()[0].id ==
           "window-2");
    assert(registry.windows()[1].id ==
           "window-1");
    assert(registry.windows()[2].id ==
           "window-4");
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "window-2"});

    const auto application =
        registry.find_application(
            "org.kde.dolphin");

    assert(application);
    assert(application->desktop_file_name ==
           "org.kde.dolphin.desktop");
    assert(application->window_ids ==
           std::vector<WindowId>({
               "window-2",
               "window-1"}));
    assert(application->active_window_id ==
           std::optional<WindowId>{
               "window-2"});

    assert(!registry.find_application(""));
    assert(!registry.find_window("window-3"));
}

void verifies_incremental_updates()
{
    FakeWindowBackend backend;
    int changes = 0;

    WindowRegistry registry(backend);

    registry.signal_changed().connect(
        [&changes]()
        {
            ++changes;
        });

    registry.start();

    const int initial_changes = changes;

    backend.add_window(
        window(
            "window-1",
            "org.kde.konsole"));

    assert(changes == initial_changes + 1);
    assert(registry.find_application(
        "org.kde.konsole.desktop"));

    auto updated_window =
        window(
            "window-1",
            "org.mozilla.firefox.desktop");

    backend.update_window(
        updated_window);

    assert(!registry.find_application(
        "org.kde.konsole"));
    assert(registry.find_application(
        "org.mozilla.firefox"));

    backend.set_active_window(
        WindowId{"window-1"});

    assert(registry.active_window() ==
           std::optional<WindowId>{
               "window-1"});

    updated_window.skip_taskbar = true;
    backend.update_window(
        updated_window);

    assert(registry.windows().empty());
    assert(registry
               .running_applications()
               .empty());
    assert(!registry.active_window());
}

void verifies_group_lifetime()
{
    FakeWindowBackend backend;

    backend.set_snapshot(
        {
            window(
                "window-1",
                "org.videolan.vlc"),
            window(
                "window-2",
                "org.videolan.vlc")
        },
        {
            "window-1",
            "window-2"
        },
        WindowId{"window-2"});

    WindowRegistry registry(backend);
    registry.start();

    auto application =
        registry.find_application(
            "org.videolan.vlc");

    assert(application);
    assert(application->window_ids.size() ==
           2);

    backend.remove_window(
        "window-1");

    application =
        registry.find_application(
            "org.videolan.vlc");

    assert(application);
    assert(application->window_ids ==
           std::vector<WindowId>({
               "window-2"}));

    backend.remove_window(
        "window-2");

    assert(!registry.find_application(
        "org.videolan.vlc"));
}

void verifies_stacking_and_disconnect()
{
    FakeWindowBackend backend;

    backend.set_snapshot(
        {
            window(
                "window-1",
                "org.kde.dolphin"),
            window(
                "window-2",
                "org.kde.dolphin"),
            window(
                "window-3",
                "org.kde.konsole")
        },
        {},
        std::nullopt);

    int changes = 0;

    WindowRegistry registry(backend);

    registry.signal_changed().connect(
        [&changes]()
        {
            ++changes;
        });

    registry.start();

    backend.set_stacking_order(
        {
            "window-3",
            "unknown-window",
            "window-2"
        });

    const int reordered_changes =
        changes;

    assert(registry.windows()[0].id ==
           "window-3");
    assert(registry.windows()[1].id ==
           "window-2");
    assert(registry.windows()[2].id ==
           "window-1");

    const auto dolphin =
        registry.find_application(
            "org.kde.dolphin");

    assert(dolphin);
    assert(dolphin->window_ids ==
           std::vector<WindowId>({
               "window-2",
               "window-1"}));

    backend.set_stacking_order(
        {
            "window-3",
            "unknown-window",
            "window-2"
        });

    assert(changes == reordered_changes);

    backend.set_connected(false);

    assert(!registry.connected());
    assert(registry.windows().empty());
    assert(registry
               .running_applications()
               .empty());
    assert(!registry.active_window());

    backend.set_snapshot(
        {
            window(
                "window-4",
                "org.mozilla.firefox")
        },
        {"window-4"},
        WindowId{"window-4"});

    backend.set_connected(true);

    assert(registry.connected());
    assert(registry.windows().size() == 1);
    assert(registry.find_application(
        "org.mozilla.firefox.desktop"));
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "window-4"});
}

void verifies_global_window_actions()
{
    FakeWindowBackend backend;

    auto active =
        window(
            "window-1",
            "org.kde.dolphin");

    auto other =
        window(
            "window-2",
            "org.kde.konsole");

    auto minimized =
        window(
            "window-3",
            "org.mozilla.firefox");

    minimized.minimized = true;

    backend.set_snapshot(
        {active, other, minimized},
        {
            "window-3",
            "window-2",
            "window-1"
        },
        WindowId{"window-1"});

    WindowRegistry registry(backend);
    registry.start();

    assert(registry.minimize_all());

    for (const auto &managed :
         registry.windows())
    {
        assert(managed.minimized);
    }

    assert(registry.unminimize_all());

    for (const auto &managed :
         registry.windows())
    {
        assert(!managed.minimized);
    }

    assert(registry.maximize_all());

    for (const auto &managed :
         registry.windows())
    {
        assert(managed.maximized);
        assert(!managed.minimized);
    }

    assert(registry.close_all());
    assert(registry.windows().empty());
}

}

int main()
{
    verifies_snapshot_and_grouping();
    verifies_incremental_updates();
    verifies_group_lifetime();
    verifies_stacking_and_disconnect();
    verifies_global_window_actions();

    return 0;
}
