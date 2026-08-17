// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// kwin_window_backend_test.cpp
//
// Test scope:
// Verifies protocol registration, atomic snapshots, incremental revision
// ordering, command dispatch, compatibility fallback, and cancellation.
//
// Tests drive the backend directly so state-machine behavior is isolated
// from D-Bus transport and a running KWin instance.
//
// ------------------------------------------------------------

#include "integrations/kwin/kwin_integration_protocol.h"
#include "integrations/kwin/kwin_window_backend.h"
#include "windowing/window_registry.h"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace
{

ManagedWindow window(
    const WindowId &id,
    const std::string &desktop_file_name)
{
    ManagedWindow window;

    window.id = id;
    window.desktop_file_name =
        desktop_file_name;

    return window;
}

void connect_with_snapshot(
    KWinWindowBackend &backend,
    std::uint64_t revision,
    const std::vector<ManagedWindow>
        &windows,
    const std::optional<WindowId>
        &active_window,
    const std::vector<WindowId>
        &stacking_order)
{
    assert(backend.register_integration(
        KWinIntegrationProtocol::VERSION));
    assert(backend.begin_snapshot(
        revision));

    for (const auto &managed_window :
         windows)
    {
        assert(backend.stage_window(
            revision,
            managed_window));
    }

    assert(backend.commit_snapshot(
        revision,
        active_window,
        stacking_order));
}

void verifies_registration_and_snapshot()
{
    KWinWindowBackend backend;

    assert(!backend.register_integration(
        KWinIntegrationProtocol::VERSION));

    backend.start();

    assert(!backend.register_integration(
        KWinIntegrationProtocol::VERSION +
        1));

    int connections = 0;

    backend
        .signal_connection_changed()
        .connect(
            [&connections](bool)
            {
                ++connections;
            });

    connect_with_snapshot(
        backend,
        10,
        {
            window(
                "window-1",
                "org.kde.dolphin"),
            window(
                "window-2",
                "org.kde.konsole")
        },
        WindowId{"window-2"},
        {
            "window-1",
            "window-2"
        });

    assert(backend.connected());
    assert(connections == 1);
    assert(backend.last_revision() == 10);
    assert(backend.windows().size() == 2);
    assert(backend.active_window() ==
           std::optional<WindowId>{
               "window-2"});
    assert(backend.windows()[1].active);
    assert(!backend.windows()[0].active);
    assert(backend.stacking_order() ==
           std::vector<WindowId>({
               "window-1",
               "window-2"}));

    int workarea_changes = 0;
    backend
        .signal_dock_workarea_geometry_changed()
        .connect(
            [&workarea_changes]()
            {
                ++workarea_changes;
            });

    assert(backend.publish_dock_workarea_geometry(
        11,
        WindowIconGeometry{
            0,
            44,
            2560,
            1396}));
    const std::optional<WindowIconGeometry>
        expected_workarea{
            {0, 44, 2560, 1396}};
    assert(backend.dock_workarea_geometry() ==
           expected_workarea);
    assert(workarea_changes == 1);
    assert(!backend.publish_dock_workarea_geometry(
        11,
        std::nullopt));

    const auto capabilities =
        backend.capabilities();

    assert(capabilities.can_activate);
    assert(capabilities.can_raise);
    assert(capabilities.can_close);
    assert(capabilities.can_minimize);
    assert(capabilities.can_maximize);
    assert(capabilities
               .provides_stacking_order);
    assert(capabilities
               .provides_frame_geometry);
    assert(capabilities
               .provides_dock_pointer_tracking);
    assert(capabilities.thumbnail_policy ==
           WindowThumbnailPolicy::
               cache_mapped_windows_after_settle);
}

void verifies_window_commands()
{
    KWinWindowBackend backend;
    backend.start();

    connect_with_snapshot(
        backend,
        15,
        {
            window(
                "window-1",
                "org.kde.dolphin")
        },
        std::nullopt,
        {"window-1"});

    std::vector<KWinWindowCommand>
        commands;

    backend.set_command_handler(
        [&commands](
            const KWinWindowCommand
                &command)
        {
            commands.push_back(command);
            return true;
        });

    assert(backend.activate_window(
        "window-1"));
    assert(backend.raise_window(
        "window-1"));
    assert(backend.close_window(
        "window-1"));
    assert(backend.set_window_minimized(
        "window-1",
        true));
    assert(backend.set_window_maximized(
        "window-1",
        false));
    assert(backend.present_windows(
        {"window-1"}));
    assert(backend.hide_windows(
        {"window-1"}));

    assert(commands.size() == 7);
    assert(commands[0].type ==
           KWinWindowCommandType::ACTIVATE);
    assert(commands[1].type ==
           KWinWindowCommandType::RAISE);
    assert(commands[2].type ==
           KWinWindowCommandType::CLOSE);
    assert(commands[3].type ==
           KWinWindowCommandType::
               SET_MINIMIZED);
    assert(commands[3].state);
    assert(commands[4].type ==
           KWinWindowCommandType::
               SET_MAXIMIZED);
    assert(!commands[4].state);
    assert(commands[5].type ==
           KWinWindowCommandType::PRESENT);
    assert(commands[5].window_ids ==
           std::vector<WindowId>({
               "window-1"}));
    assert(commands[6].type ==
           KWinWindowCommandType::HIDE);
    assert(commands[6].window_ids ==
           std::vector<WindowId>({
               "window-1"}));

    assert(!backend.activate_window(
        "missing-window"));

    backend.set_command_handler({});

    assert(!backend.activate_window(
        "window-1"));
}

void verifies_legacy_script_uses_present_fallback()
{
    KWinWindowBackend backend;
    backend.start();

    auto first =
        window(
            "window-1",
            "org.kde.dolphin");
    auto second =
        window(
            "window-2",
            "org.kde.dolphin");
    first.minimized = true;
    second.minimized = true;

    assert(backend.register_integration(
        KWinIntegrationProtocol::
            LEGACY_VERSION));
    assert(backend.begin_snapshot(16));
    assert(backend.stage_window(
        16,
        first));
    assert(backend.stage_window(
        16,
        second));
    assert(backend.commit_snapshot(
        16,
        std::nullopt,
        {
            "window-1",
            "window-2"
        }));

    std::vector<KWinWindowCommand>
        commands;
    backend.set_command_handler(
        [&commands](
            const KWinWindowCommand
                &command)
        {
            commands.push_back(command);
            return true;
        });

    assert(backend.present_windows(
        {
            "window-1",
            "window-2"
        }));
    assert(commands.size() == 5);
    assert(commands[0].type ==
           KWinWindowCommandType::
               SET_MINIMIZED);
    assert(commands[1].type ==
           KWinWindowCommandType::RAISE);
    assert(commands[2].type ==
           KWinWindowCommandType::
               SET_MINIMIZED);
    assert(commands[3].type ==
           KWinWindowCommandType::RAISE);
    assert(commands[4].type ==
           KWinWindowCommandType::ACTIVATE);

    commands.clear();
    assert(backend.hide_windows(
        {
            "window-1",
            "window-2"
        }));
    assert(commands.size() == 2);
    assert(commands[0].type ==
           KWinWindowCommandType::
               SET_MINIMIZED);
    assert(commands[0].state);
    assert(commands[1].type ==
           KWinWindowCommandType::
               SET_MINIMIZED);
    assert(commands[1].state);
}

void verifies_incremental_revisions()
{
    KWinWindowBackend backend;
    backend.start();

    connect_with_snapshot(
        backend,
        20,
        {
            window(
                "window-1",
                "org.kde.dolphin")
        },
        std::nullopt,
        {"window-1"});

    auto updated_window =
        window(
            "window-1",
            "org.kde.dolphin");

    updated_window.caption =
        "Home";
    updated_window.desktop_ids =
        {"desktop-1"};
    updated_window.desktop_numbers =
        {1};
    updated_window.on_current_desktop =
        false;

    assert(backend.publish_window(
        21,
        updated_window));
    assert(backend.windows()[0].caption ==
           "Home");
    assert(!backend.windows()[0]
                .on_current_desktop);

    updated_window.caption =
        "Stale title";

    assert(!backend.publish_window(
        21,
        updated_window));
    assert(!backend.publish_window(
        19,
        updated_window));
    assert(backend.windows()[0].caption ==
           "Home");

    assert(backend.publish_active_window(
        22,
        WindowId{"window-1"}));
    assert(backend.windows()[0].active);

    assert(backend.publish_current_desktop(
        23,
        "desktop-1",
        1));
    assert(backend.windows()[0]
               .on_current_desktop);

    updated_window.caption =
        "Current desktop update";
    updated_window.on_current_desktop =
        false;

    assert(backend.publish_window(
        24,
        updated_window));
    assert(backend.windows()[0]
               .on_current_desktop);

    assert(backend.publish_stacking_order(
        25,
        {"window-1"}));

    assert(backend.publish_window_removed(
        26,
        "window-1"));
    assert(backend.windows().empty());
    assert(!backend.active_window());
    assert(backend.stacking_order().empty());
    assert(backend.last_revision() == 26);
}

void verifies_atomic_resynchronization()
{
    KWinWindowBackend backend;
    WindowRegistry registry(backend);

    registry.start();

    assert(!registry.connected());

    connect_with_snapshot(
        backend,
        30,
        {
            window(
                "window-1",
                "org.kde.dolphin")
        },
        WindowId{"window-1"},
        {"window-1"});

    assert(registry.connected());
    assert(registry.find_application(
        "org.kde.dolphin.desktop"));

    int registry_changes = 0;

    registry.signal_changed().connect(
        [&registry_changes]()
        {
            ++registry_changes;
        });

    assert(backend.register_integration(
        KWinIntegrationProtocol::VERSION));
    assert(backend.connected());
    assert(registry.connected());
    assert(registry.windows().size() == 1);
    assert(registry.find_window("window-1"));
    assert(registry_changes == 0);
    assert(backend.last_revision() == 0);

    assert(backend.begin_snapshot(1));
    assert(!backend.begin_snapshot(1));
    assert(!backend.stage_window(
        2,
        window(
            "wrong-revision",
            "org.kde.konsole")));
    assert(backend.stage_window(
        1,
        window(
            "window-2",
            "org.mozilla.firefox")));
    assert(backend.commit_snapshot(
        1,
        WindowId{"window-2"},
        {"window-2"}));

    assert(registry_changes == 1);
    assert(registry.windows().size() == 1);
    assert(!registry.find_window("window-1"));
    assert(registry.find_application(
        "org.mozilla.firefox.desktop"));
    assert(registry.active_window() ==
           std::optional<WindowId>{
               "window-2"});

    backend.unregister_integration();

    assert(!backend.connected());
    assert(!registry.connected());
    assert(registry.windows().empty());
}

void verifies_cancelled_snapshot()
{
    KWinWindowBackend backend;
    backend.start();

    assert(backend.register_integration(
        KWinIntegrationProtocol::VERSION));
    assert(backend.begin_snapshot(50));
    assert(backend.stage_window(
        50,
        window(
            "window-1",
            "org.kde.dolphin")));

    backend.cancel_snapshot();

    assert(!backend.commit_snapshot(
        50,
        std::nullopt,
        {}));
    assert(!backend.connected());
    assert(backend.windows().empty());
}

}

int main()
{
    verifies_registration_and_snapshot();
    verifies_window_commands();
    verifies_legacy_script_uses_present_fallback();
    verifies_incremental_revisions();
    verifies_atomic_resynchronization();
    verifies_cancelled_snapshot();

    return 0;
}
