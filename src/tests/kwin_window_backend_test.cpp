#include "kwin_integration_protocol.h"
#include "kwin_window_backend.h"
#include "window_registry.h"

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

    const auto capabilities =
        backend.capabilities();

    assert(!capabilities.can_activate);
    assert(!capabilities.can_close);
    assert(capabilities
               .provides_stacking_order);
    assert(capabilities
               .provides_frame_geometry);
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

    assert(backend.publish_window(
        21,
        updated_window));
    assert(backend.windows()[0].caption ==
           "Home");

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

    assert(backend.publish_stacking_order(
        23,
        {"window-1"}));

    assert(backend.publish_window_removed(
        24,
        "window-1"));
    assert(backend.windows().empty());
    assert(!backend.active_window());
    assert(backend.stacking_order().empty());
    assert(backend.last_revision() == 24);
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

    assert(backend.begin_snapshot(40));
    assert(!backend.begin_snapshot(39));
    assert(!backend.stage_window(
        39,
        window(
            "wrong-revision",
            "org.kde.konsole")));
    assert(backend.stage_window(
        40,
        window(
            "window-2",
            "org.mozilla.firefox")));
    assert(backend.commit_snapshot(
        40,
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
    verifies_incremental_revisions();
    verifies_atomic_resynchronization();
    verifies_cancelled_snapshot();

    return 0;
}
