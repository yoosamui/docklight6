// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// window_registry_test.cpp
//
// Test scope:
// Exercises snapshot and incremental synchronization, application
// grouping, identity fallback, stacking, disconnects, and global actions.
//
// FakeWindowBackend isolates registry policy; temporary desktop entries
// cover executable-to-application identity resolution.
//
// ------------------------------------------------------------

#include "fake_window_backend.h"
#include "windowing/window_registry.h"

#include <giomm/init.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <fstream>
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

std::string executable_desktop_id()
{
    GError *error = nullptr;
    auto executable_path =
        g_file_read_link(
            "/proc/self/exe",
            &error);

    assert(executable_path);
    assert(!error);

    auto executable_name =
        g_path_get_basename(
            executable_path);

    assert(executable_name);

    std::string desktop_id =
        executable_name;

    g_free(executable_name);
    g_free(executable_path);

    std::transform(
        desktop_id.begin(),
        desktop_id.end(),
        desktop_id.begin(),
        [](unsigned char character)
        {
            if (std::isspace(character) ||
                character == '_')
            {
                return '-';
            }

            return static_cast<char>(
                std::tolower(character));
        });

    desktop_id += ".desktop";
    return desktop_id;
}

std::string install_test_desktop_file()
{
    GError *error = nullptr;
    auto temporary_directory =
        g_dir_make_tmp(
            "docklight-registry-XXXXXX",
            &error);

    assert(temporary_directory);
    assert(!error);

    const std::string data_directory =
        temporary_directory;

    g_free(temporary_directory);

    const auto applications_directory =
        data_directory + "/applications";

    assert(
        g_mkdir_with_parents(
            applications_directory.c_str(),
            0700) == 0);

    const auto desktop_file =
        applications_directory + "/" +
        executable_desktop_id();

    {
        std::ofstream output(
            desktop_file);

        output
            << "[Desktop Entry]\n"
            << "Type=Application\n"
            << "Name=Window Registry Test\n"
            << "Exec=/bin/true\n"
            << "Icon=application-x-executable\n";

        assert(output);
    }

    assert(
        g_setenv(
            "XDG_DATA_HOME",
            data_directory.c_str(),
            true));

    return data_directory;
}

void remove_test_desktop_file(
    const std::string &data_directory)
{
    const auto applications_directory =
        data_directory + "/applications";
    const auto desktop_file =
        applications_directory + "/" +
        executable_desktop_id();

    g_remove(desktop_file.c_str());
    g_rmdir(applications_directory.c_str());
    g_rmdir(data_directory.c_str());
}

void verifies_process_executable_fallback()
{
    FakeWindowBackend backend;

    auto transient_window =
        window(
            "transient-window",
            "org.example.dynamic-123-456");

    transient_window.process_id =
        static_cast<std::int64_t>(
            getpid());

    backend.set_snapshot(
        {transient_window},
        {"transient-window"},
        WindowId{"transient-window"});

    WindowRegistry registry(backend);
    registry.start();

    const auto canonical_id =
        executable_desktop_id();

    assert(registry.find_application(
        canonical_id));
    assert(!registry.find_application(
        "org.example.dynamic-123-456"));
    assert(registry.windows().size() == 1);
    assert(
        registry.windows()[0]
            .desktop_file_name ==
        canonical_id);
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
                ""),
            window(
                "window-5",
                "mullvad-browser"),
            window(
                "window-6",
                "Mullvad Browser")
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
    assert(registry.windows().size() == 5);
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

    const auto mullvad =
        registry.find_application(
            "mullvad-browser");

    assert(mullvad);
    assert(mullvad->window_ids ==
           std::vector<WindowId>({
               "window-5",
               "window-6"}));
}

void verifies_incremental_updates()
{
    FakeWindowBackend backend;
    int changes = 0;
    int geometry_changes = 0;

    WindowRegistry registry(backend);

    registry.signal_changed().connect(
        [&changes]()
        {
            ++changes;
        });

    registry
        .signal_window_geometry_changed()
        .connect(
            [&geometry_changes]()
            {
                ++geometry_changes;
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

    const int changes_before_move =
        changes;
    const int geometry_changes_before_move =
        geometry_changes;

    updated_window.frame_geometry.x = 320;
    updated_window.frame_geometry.y = 180;
    backend.update_window(
        updated_window);

    assert(changes ==
           changes_before_move);
    assert(geometry_changes ==
           geometry_changes_before_move + 1);
    assert(registry
               .find_window("window-1")
               ->frame_geometry.x == 320);
    assert(registry
               .find_window("window-1")
               ->frame_geometry.y == 180);

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

    auto docklight =
        window(
            "docklight-window",
            "org.docklight6");

    minimized.minimized = true;

    backend.set_snapshot(
        {
            active,
            other,
            minimized,
            docklight
        },
        {
            "docklight-window",
            "window-3",
            "window-2",
            "window-1"
        },
        WindowId{"window-1"});

    WindowRegistry registry(backend);
    registry.start();

    assert(!registry.find_window(
        "docklight-window"));

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
    assert(backend.windows().size() == 1);
    assert(backend.windows()[0].id ==
           "docklight-window");
}

}

int main()
{
    Gio::init();

    const auto test_data_directory =
        install_test_desktop_file();

    verifies_process_executable_fallback();
    verifies_snapshot_and_grouping();
    verifies_incremental_updates();
    verifies_group_lifetime();
    verifies_stacking_and_disconnect();
    verifies_global_window_actions();

    remove_test_desktop_file(
        test_data_directory);

    return 0;
}
