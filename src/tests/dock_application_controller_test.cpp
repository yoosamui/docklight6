// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_application_controller_test.cpp
//
// Test scope:
// Exercises grouped launcher actions, desktop-aware activation and
// minimization, alternate identities, window entries, and cycle state.
//
// The tests use FakeWindowBackend through WindowRegistry so controller
// policy is verified without a running compositor or GTK session.
//
// ------------------------------------------------------------

#include "application/dock_application_controller.h"
#include "fake_window_backend.h"
#include "windowing/window_registry.h"

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
    assert(controller.window_count() == 2);
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
    assert(controller.window_count() == 0);
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
    assert(!controller.cycle_window(
        WindowCycleDirection::next));
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

void verifies_inactive_window_is_activated()
{
    FakeWindowBackend backend;

    auto application_window =
        window("window-1");
    auto covering_window =
        window(
            "window-2",
            false,
            "org.mozilla.firefox");

    backend.set_snapshot(
        {
            application_window,
            covering_window
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
            "org.kde.dolphin.desktop"
        });

    assert(controller
               .toggle_minimized());
    assert(!registry
                .find_window("window-1")
                ->minimized);
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "window-1"});
}

void verifies_frontmost_group_hides_without_active_window()
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
        std::nullopt);

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {
            "org.kde.dolphin.desktop"
        });

    assert(controller.toggle_minimized());
    assert(registry
               .find_window("window-1")
               ->minimized);
    assert(registry
               .find_window("window-2")
               ->minimized);
}

void verifies_fully_minimized_group_is_presented_once()
{
    FakeWindowBackend backend;

    backend.set_snapshot(
        {
            window("window-1"),
            window("window-2"),
            window("window-3"),
            window(
                "covering-window",
                false,
                "org.mozilla.firefox")
        },
        {
            "window-1",
            "window-2",
            "window-3",
            "covering-window"
        },
        WindowId{"window-3"});

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {
            "org.kde.dolphin.desktop"
        });

    assert(controller.toggle_minimized());
    backend.set_active_window(
        WindowId{"covering-window"});

    unsigned int stacking_changes = 0;
    backend
        .signal_stacking_order_changed()
        .connect(
            [&stacking_changes](
                const std::vector<WindowId> &)
            {
                ++stacking_changes;
            });

    assert(controller.toggle_minimized());
    assert(stacking_changes == 1);
    assert(!registry
                .find_window("window-1")
                ->minimized);
    assert(!registry
                .find_window("window-2")
                ->minimized);
    assert(!registry
                .find_window("window-3")
                ->minimized);
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "window-3"});
    assert(backend.stacking_order() ==
           std::vector<WindowId>({
               "covering-window",
               "window-1",
               "window-2",
               "window-3"}));
}

void verifies_other_desktop_window_is_activated()
{
    FakeWindowBackend backend;

    auto application_window =
        window("window-1");
    application_window.on_current_desktop =
        false;

    backend.set_snapshot(
        {
            application_window
        },
        {
            "window-1"
        },
        std::nullopt);

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {
            "org.kde.dolphin.desktop"
        });

    assert(controller
               .toggle_minimized());
    assert(!registry
                .find_window("window-1")
                ->minimized);
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "window-1"});
}

void verifies_desktop_groups_are_activated_in_order()
{
    FakeWindowBackend backend;

    auto desktop_three_open =
        window("desktop-3-open");
    auto desktop_three_minimized =
        window(
            "desktop-3-minimized",
            true);
    auto desktop_four_open =
        window("desktop-4-open");
    auto desktop_four_minimized =
        window(
            "desktop-4-minimized",
            true);
    auto current_desktop_window =
        window(
            "desktop-1-other-app",
            false,
            "org.mozilla.firefox");

    desktop_three_open.desktop_ids =
        {"desktop-3"};
    desktop_three_open.desktop_numbers =
        {3};
    desktop_three_open.on_current_desktop =
        false;
    desktop_three_minimized.desktop_ids =
        {"desktop-3"};
    desktop_three_minimized.desktop_numbers =
        {3};
    desktop_three_minimized
        .on_current_desktop = false;

    desktop_four_open.desktop_ids =
        {"desktop-4"};
    desktop_four_open.desktop_numbers =
        {4};
    desktop_four_open.on_current_desktop =
        false;
    desktop_four_minimized.desktop_ids =
        {"desktop-4"};
    desktop_four_minimized.desktop_numbers =
        {4};
    desktop_four_minimized
        .on_current_desktop = false;

    backend.set_snapshot(
        {
            desktop_three_minimized,
            desktop_three_open,
            desktop_four_minimized,
            desktop_four_open,
            current_desktop_window
        },
        {
            "desktop-3-minimized",
            "desktop-3-open",
            "desktop-4-minimized",
            "desktop-4-open",
            "desktop-1-other-app"
        },
        WindowId{
            "desktop-1-other-app"});

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {
            "org.kde.dolphin.desktop"
        });

    // Desktop 4 is the most recent group with an open window.
    assert(controller
               .toggle_minimized());
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "desktop-4-open"});
    assert(!registry
                .find_window(
                    "desktop-4-minimized")
                ->minimized);
    assert(registry
               .find_window(
                   "desktop-3-minimized")
               ->minimized);

    // Simulate KWin's desktop-presence update after activation.
    for (const auto &window_id :
         std::vector<WindowId>{
             "desktop-4-minimized",
             "desktop-4-open"})
    {
        auto updated =
            *registry.find_window(
                window_id);
        updated.on_current_desktop = true;
        backend.update_window(updated);
    }

    // A second click hides the complete application group without switching
    // away from Desktop 4.
    assert(controller
               .toggle_minimized());
    assert(registry
               .find_window(
                   "desktop-4-minimized")
               ->minimized);
    assert(registry
               .find_window(
                   "desktop-4-open")
               ->minimized);
    assert(registry
               .find_window(
                   "desktop-3-open")
               ->minimized);
    assert(registry
               .find_window(
                   "desktop-3-minimized")
               ->minimized);

    backend.set_active_window(
        std::nullopt);

    // With every group hidden, the next click restores every desktop's
    // windows in place and activates only the current desktop's group.
    assert(controller
               .toggle_minimized());
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "desktop-4-open"});
    assert(!registry
                .find_window(
                    "desktop-4-open")
                ->minimized);
    assert(!registry
               .find_window(
                   "desktop-3-open")
               ->minimized);
    assert(!registry
                .find_window(
                    "desktop-3-minimized")
                ->minimized);
}

void verifies_minimized_current_group_precedes_open_remote_group()
{
    FakeWindowBackend backend;

    auto desktop_two_window =
        window("desktop-2-window");
    auto desktop_four_first =
        window("desktop-4-first", true);
    auto desktop_four_second =
        window("desktop-4-second", true);

    desktop_two_window.desktop_ids =
        {"desktop-2"};
    desktop_two_window.desktop_numbers =
        {2};
    desktop_two_window.on_current_desktop =
        false;

    desktop_four_first.desktop_ids =
        {"desktop-4"};
    desktop_four_first.desktop_numbers =
        {4};
    desktop_four_second.desktop_ids =
        {"desktop-4"};
    desktop_four_second.desktop_numbers =
        {4};

    backend.set_snapshot(
        {
            desktop_two_window,
            desktop_four_first,
            desktop_four_second
        },
        {
            "desktop-2-window",
            "desktop-4-first",
            "desktop-4-second"
        },
        WindowId{"desktop-2-window"});

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {
            "org.kde.dolphin.desktop"
        });

    assert(controller.toggle_minimized());
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "desktop-4-second"});
    assert(!registry
                .find_window(
                    "desktop-4-first")
                ->minimized);
    assert(!registry
                .find_window(
                    "desktop-4-second")
                ->minimized);
    assert(!registry
                .find_window(
                    "desktop-2-window")
                ->on_current_desktop);
}

void verifies_all_remote_groups_are_restored()
{
    FakeWindowBackend backend;

    auto desktop_one = window("desktop-1", true);
    auto desktop_two = window("desktop-2", true);
    desktop_one.desktop_numbers = {1};
    desktop_one.on_current_desktop = false;
    desktop_two.desktop_numbers = {2};
    desktop_two.on_current_desktop = false;

    backend.set_snapshot(
        {desktop_one, desktop_two},
        {"desktop-1", "desktop-2"},
        std::nullopt);

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {"org.kde.dolphin.desktop"});

    assert(controller.toggle_minimized());
    assert(!registry.find_window("desktop-1")->minimized);
    assert(!registry.find_window("desktop-2")->minimized);
    assert(registry.active_window() ==
           std::optional<WindowId>{"desktop-2"});
}

void verifies_active_group_hides_all_desktops()
{
    FakeWindowBackend backend;

    auto desktop_one_window =
        window("desktop-1-window");
    auto desktop_four_first =
        window("desktop-4-first");
    auto desktop_four_second =
        window("desktop-4-second");

    desktop_one_window.desktop_ids =
        {"desktop-1"};
    desktop_one_window.desktop_numbers =
        {1};
    desktop_four_first.desktop_ids =
        {"desktop-4"};
    desktop_four_first.desktop_numbers =
        {4};
    desktop_four_first.on_current_desktop =
        false;
    desktop_four_second.desktop_ids =
        {"desktop-4"};
    desktop_four_second.desktop_numbers =
        {4};
    desktop_four_second.on_current_desktop =
        false;

    backend.set_snapshot(
        {
            desktop_one_window,
            desktop_four_first,
            desktop_four_second
        },
        {
            "desktop-4-first",
            "desktop-4-second",
            "desktop-1-window"
        },
        WindowId{"desktop-1-window"});

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {
            "org.kde.dolphin.desktop"
        });

    assert(controller
               .toggle_minimized());
    assert(registry
               .find_window(
                   "desktop-1-window")
               ->minimized);
    assert(registry
               .find_window(
                   "desktop-4-first")
               ->minimized);
    assert(registry
               .find_window(
                   "desktop-4-second")
               ->minimized);

    // No activation was dispatched, so the selected desktop/window did not
    // change while the complete group was hidden.
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "desktop-1-window"});
}

void verifies_current_desktop_only_activation()
{
    FakeWindowBackend backend;

    auto current_window =
        window("desktop-2-window");
    auto remote_window =
        window("desktop-4-window");

    current_window.desktop_ids =
        {"desktop-2"};
    current_window.desktop_numbers =
        {2};
    remote_window.desktop_ids =
        {"desktop-4"};
    remote_window.desktop_numbers =
        {4};
    remote_window.on_current_desktop =
        false;

    backend.set_snapshot(
        {
            current_window,
            remote_window
        },
        {
            "desktop-4-window",
            "desktop-2-window"
        },
        WindowId{"desktop-2-window"});

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {
            "org.kde.dolphin.desktop"
        });
    controller.set_manage_all_workspaces(
        false);

    assert(controller.toggle_minimized());
    assert(registry
               .find_window(
                   "desktop-2-window")
               ->minimized);
    assert(!registry
                .find_window(
                    "desktop-4-window")
                ->minimized);

    backend.set_active_window(
        std::nullopt);

    assert(controller.toggle_minimized());
    assert(!registry
                .find_window(
                    "desktop-2-window")
                ->minimized);
    assert(!registry
                .find_window(
                    "desktop-4-window")
                ->minimized);

    auto updated_current =
        *registry.find_window(
            "desktop-2-window");
    updated_current.on_current_desktop =
        false;
    backend.update_window(
        updated_current);
    backend.set_active_window(
        std::nullopt);

    assert(!controller.toggle_minimized());
    assert(!registry.active_window());
}

void verifies_window_cycling()
{
    FakeWindowBackend backend;

    backend.set_snapshot(
        {
            window("window-1", true),
            window("window-2"),
            window("window-3")
        },
        {
            "window-1",
            "window-2",
            "window-3"
        },
        WindowId{"window-3"});

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {
            "org.kde.dolphin.desktop"
        });
    controller.set_manage_all_workspaces(
        false);

    assert(controller.cycle_window(
        WindowCycleDirection::next));
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "window-1"});
    assert(!registry
                .find_window("window-1")
                ->minimized);

    assert(controller.cycle_window(
        WindowCycleDirection::next));
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "window-2"});

    assert(controller.cycle_window(
        WindowCycleDirection::next));
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "window-3"});

    controller.reset_window_cycle();

    assert(controller.cycle_window(
        WindowCycleDirection::previous));
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "window-2"});

    assert(controller.cycle_window(
        WindowCycleDirection::previous));
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "window-1"});

    assert(controller.cycle_window(
        WindowCycleDirection::previous));
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "window-3"});
}

void verifies_window_cycle_uses_all_desktops_when_enabled()
{
    FakeWindowBackend backend;

    auto remote_window =
        window("desktop-4-window");
    remote_window.on_current_desktop =
        false;
    remote_window.desktop_ids =
        {"desktop-4"};
    remote_window.desktop_numbers =
        {4};

    backend.set_snapshot(
        {
            window("desktop-2-first"),
            remote_window,
            window("desktop-2-second")
        },
        {
            "desktop-2-first",
            "desktop-4-window",
            "desktop-2-second"
        },
        WindowId{"desktop-2-first"});

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {
            "org.kde.dolphin.desktop"
        });

    assert(controller.cycle_window(
        WindowCycleDirection::next));
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "desktop-4-window"});
}

void verifies_window_cycle_tracks_group_changes()
{
    FakeWindowBackend backend;

    backend.set_snapshot(
        {
            window("window-1"),
            window("window-2"),
            window("window-3")
        },
        {
            "window-1",
            "window-2",
            "window-3"
        },
        WindowId{"window-3"});

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {
            "org.kde.dolphin.desktop"
        });

    assert(controller.cycle_window(
        WindowCycleDirection::next));
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "window-1"});

    backend.remove_window("window-2");

    assert(controller.cycle_window(
        WindowCycleDirection::next));
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "window-3"});
}

void verifies_window_cycle_stays_on_current_desktop()
{
    FakeWindowBackend backend;

    auto other_desktop_window =
        window("desktop-4-window");
    other_desktop_window
        .on_current_desktop = false;
    other_desktop_window.desktop_ids =
        {"desktop-4"};
    other_desktop_window.desktop_numbers =
        {4};

    backend.set_snapshot(
        {
            window("desktop-1-first"),
            other_desktop_window,
            window("desktop-1-second")
        },
        {
            "desktop-1-first",
            "desktop-4-window",
            "desktop-1-second"
        },
        WindowId{"desktop-1-second"});

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {
            "org.kde.dolphin.desktop"
        });
    controller.set_manage_all_workspaces(
        false);

    assert(controller.cycle_window(
        WindowCycleDirection::next));
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "desktop-1-first"});

    assert(controller.cycle_window(
        WindowCycleDirection::next));
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "desktop-1-second"});

    assert(controller.cycle_window(
        WindowCycleDirection::previous));
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "desktop-1-first"});
}

void verifies_grouped_window_entries()
{
    FakeWindowBackend backend;

    auto first_window =
        window("window-1", true);
    auto second_window =
        window("window-2");
    auto other_application_window =
        window(
            "window-3",
            false,
            "org.mozilla.firefox");

    first_window.caption =
        "Files";
    first_window.icon_name =
        "system-file-manager";
    first_window.frame_geometry = {
        10,
        20,
        800,
        600};
    first_window.desktop_numbers =
        {2};
    first_window.on_current_desktop =
        false;
    second_window.caption =
        "Downloads";
    second_window.icon_name =
        "folder-download";
    second_window.skip_taskbar = true;
    second_window.include_when_skip_taskbar = true;

    backend.set_snapshot(
        {
            first_window,
            second_window,
            other_application_window
        },
        {
            "window-1",
            "window-2",
            "window-3"
        },
        WindowId{"window-2"});

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {
            "org.kde.dolphin.desktop"
        });

    const auto entries =
        controller.window_entries();

    assert(entries.size() == 2);
    assert(entries[0].id ==
           "window-1");
    assert(entries[0].caption ==
           "Files");
    assert(entries[0].icon_name ==
           "system-file-manager");
    assert(entries[0].frame_geometry.x == 10);
    assert(entries[0].frame_geometry.y == 20);
    assert(entries[0].frame_geometry.width == 800);
    assert(entries[0].frame_geometry.height == 600);
    assert(entries[0].desktop_numbers ==
           std::vector<unsigned int>{2});
    assert(!entries[0]
                .on_current_desktop);
    assert(!entries[0].active);
    assert(entries[0].minimized);

    assert(entries[1].id ==
           "window-2");
    assert(entries[1].caption ==
           "Downloads");
    assert(entries[1]
               .desktop_numbers.empty());
    assert(entries[1]
               .on_current_desktop);
    assert(entries[1].active);
    assert(!entries[1].minimized);
    assert(entries[1].application_auxiliary);

    controller.set_manage_all_workspaces(
        false);

    const auto current_desktop_entries =
        controller.window_entries();

    assert(current_desktop_entries.size() == 1);
    assert(current_desktop_entries[0].id ==
           "window-2");

    controller.set_manage_all_workspaces(
        true);

    assert(!controller.show_window(
        "window-3"));
    assert(controller.show_window(
        "window-1"));
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "window-1"});
    assert(!registry
                .find_window("window-1")
                ->minimized);

    assert(controller.minimize_window(
        "window-2"));
    assert(registry
               .find_window("window-2")
               ->minimized);
    assert(!registry
                .find_window("window-1")
                ->minimized);
    assert(!controller.minimize_window(
        "window-3"));
    assert(!controller.close_window(
        "window-3"));
    assert(controller.close_window(
        "window-2"));
    assert(!registry.find_window("window-2"));
}

void verifies_unfocusable_auxiliary_preview_toggles()
{
    FakeWindowBackend backend;

    auto browser = window(
        "browser-window",
        false,
        "org.mozilla.firefox");
    auto picture_in_picture = window(
        "picture-in-picture",
        false,
        "org.mozilla.firefox");
    picture_in_picture.skip_taskbar = true;
    picture_in_picture.include_when_skip_taskbar = true;

    backend.set_snapshot(
        {browser, picture_in_picture},
        {"browser-window", "picture-in-picture"},
        WindowId{"browser-window"});

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {"org.mozilla.firefox.desktop"});

    // Mutter can ignore minimize for an unfocusable PiP and keep reporting it
    // as unminimized. Its preview action must therefore show/raise the real
    // client instead of entering a repeated set-minimized loop.
    assert(controller.toggle_window(
        "picture-in-picture"));
    assert(!registry
                .find_window("picture-in-picture")
                ->minimized);
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "picture-in-picture"});

    picture_in_picture = *registry.find_window(
        "picture-in-picture");
    picture_in_picture.minimized = true;
    backend.update_window(picture_in_picture);
    assert(controller.toggle_window(
        "picture-in-picture"));
    assert(!registry
                .find_window("picture-in-picture")
                ->minimized);
}

void verifies_launcher_click_ignores_unminimizable_auxiliary()
{
    FakeWindowBackend backend;

    auto browser = window(
        "browser-window",
        true,
        "org.mozilla.firefox");
    auto picture_in_picture = window(
        "picture-in-picture",
        false,
        "org.mozilla.firefox");
    picture_in_picture.skip_taskbar = true;
    picture_in_picture.include_when_skip_taskbar = true;

    backend.set_snapshot(
        {browser, picture_in_picture},
        {"browser-window", "picture-in-picture"},
        "picture-in-picture");

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {"org.mozilla.firefox.desktop"});

    assert(controller.toggle_minimized());
    assert(!registry
                .find_window("browser-window")
                ->minimized);
    assert(!registry
                .find_window("picture-in-picture")
                ->minimized);
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "browser-window"});

    assert(controller.toggle_minimized());
    assert(registry
               .find_window("browser-window")
               ->minimized);
    assert(!registry
                .find_window("picture-in-picture")
                ->minimized);
}

void verifies_remote_normal_group_toggles_instead_of_current_pip()
{
    FakeWindowBackend backend;

    auto browser_one = window(
        "browser-one",
        true,
        "org.mozilla.firefox");
    browser_one.on_current_desktop = false;
    browser_one.desktop_numbers = {2};

    auto browser_two = window(
        "browser-two",
        true,
        "org.mozilla.firefox");
    browser_two.on_current_desktop = false;
    browser_two.desktop_numbers = {2};

    auto picture_in_picture = window(
        "picture-in-picture",
        false,
        "org.mozilla.firefox");
    picture_in_picture.desktop_numbers = {1};
    picture_in_picture.skip_taskbar = true;
    picture_in_picture.include_when_skip_taskbar = true;

    backend.set_snapshot(
        {browser_one, browser_two, picture_in_picture},
        {"browser-one", "browser-two", "picture-in-picture"},
        "picture-in-picture");

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {"org.mozilla.firefox.desktop"});

    assert(controller.toggle_minimized());
    assert(!registry
                .find_window("browser-one")
                ->minimized);
    assert(!registry
                .find_window("browser-two")
                ->minimized);
    assert(!registry
                .find_window("picture-in-picture")
                ->minimized);
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "browser-two"});
}

void verifies_normal_group_reveals_while_pip_stays_visible()
{
    FakeWindowBackend backend;

    auto browser_one = window(
        "browser-one",
        false,
        "org.mozilla.firefox");
    auto browser_two = window(
        "browser-two",
        false,
        "org.mozilla.firefox");
    auto picture_in_picture = window(
        "picture-in-picture",
        false,
        "org.mozilla.firefox");
    picture_in_picture.skip_taskbar = true;
    picture_in_picture.include_when_skip_taskbar = true;

    backend.set_snapshot(
        {browser_one, browser_two, picture_in_picture},
        {"browser-one", "browser-two", "picture-in-picture"},
        "picture-in-picture");

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {"org.mozilla.firefox.desktop"});

    assert(controller.toggle_minimized());
    assert(registry.find_window("browser-one")->minimized);
    assert(registry.find_window("browser-two")->minimized);
    assert(!registry
                .find_window("picture-in-picture")
                ->minimized);

    assert(controller.toggle_minimized());
    assert(!registry.find_window("browser-one")->minimized);
    assert(!registry.find_window("browser-two")->minimized);
    assert(!registry
                .find_window("picture-in-picture")
                ->minimized);
    assert(registry.active_window() ==
           std::optional<WindowId>{"browser-two"});
}

void verifies_pip_group_reveals_before_hide_state_arrives()
{
    FakeWindowBackend backend;

    auto browser = window(
        "browser-window",
        false,
        "org.mozilla.firefox");
    auto picture_in_picture = window(
        "picture-in-picture",
        false,
        "org.mozilla.firefox");
    picture_in_picture.skip_taskbar = true;
    picture_in_picture.include_when_skip_taskbar = true;

    backend.set_snapshot(
        {browser, picture_in_picture},
        {"browser-window", "picture-in-picture"},
        "picture-in-picture");

    WindowRegistry registry(backend);
    registry.start();

    DockApplicationController controller(
        &registry,
        {"org.mozilla.firefox.desktop"});

    assert(controller.toggle_minimized());
    assert(registry.find_window("browser-window")->minimized);

    // Reproduce a delayed compositor notification: the hide command was
    // accepted, but the controller's snapshot still says the normal window
    // is visible when the next DockItem click arrives.
    browser.minimized = false;
    backend.update_window(browser);

    assert(controller.toggle_minimized());
    assert(!registry.find_window("browser-window")->minimized);
    assert(!registry
                .find_window("picture-in-picture")
                ->minimized);
    assert(registry.active_window() ==
           std::optional<WindowId>{"browser-window"});
}

void verifies_tiling_backend_focuses_and_cycles()
{
    FakeWindowBackend backend;
    backend.set_minimize_supported(false);
    backend.set_snapshot(
        {
            window("window-1"),
            window("window-2"),
            window(
                "other-window",
                false,
                "org.mozilla.firefox")
        },
        {"window-1", "window-2", "other-window"},
        "other-window");

    WindowRegistry registry(backend);
    registry.start();
    DockApplicationController controller(
        &registry,
        {"org.kde.dolphin.desktop"});

    assert(!controller.can_minimize());
    assert(controller.toggle_minimized());
    assert(registry.active_window() ==
           std::optional<WindowId>{"window-2"});
    assert(controller.toggle_minimized());
    assert(registry.active_window() ==
           std::optional<WindowId>{"window-1"});
    assert(!registry.find_window("window-1")->minimized);
    assert(!registry.find_window("window-2")->minimized);
}

}

int main()
{
    verifies_launcher_window_actions();
    verifies_missing_application();
    verifies_alternate_application_identity();
    verifies_inactive_window_is_activated();
    verifies_frontmost_group_hides_without_active_window();
    verifies_fully_minimized_group_is_presented_once();
    verifies_other_desktop_window_is_activated();
    verifies_desktop_groups_are_activated_in_order();
    verifies_minimized_current_group_precedes_open_remote_group();
    verifies_all_remote_groups_are_restored();
    verifies_active_group_hides_all_desktops();
    verifies_current_desktop_only_activation();
    verifies_window_cycling();
    verifies_window_cycle_tracks_group_changes();
    verifies_window_cycle_stays_on_current_desktop();
    verifies_window_cycle_uses_all_desktops_when_enabled();
    verifies_grouped_window_entries();
    verifies_unfocusable_auxiliary_preview_toggles();
    verifies_launcher_click_ignores_unminimizable_auxiliary();
    verifies_remote_normal_group_toggles_instead_of_current_pip();
    verifies_normal_group_reveals_while_pip_stays_visible();
    verifies_pip_group_reveals_before_hide_state_arrives();
    verifies_tiling_backend_focuses_and_cycles();

    return 0;
}
