// ------------------------------------------------------------
// Docklight 6.0
//
// Verifies Hyprland JSON normalization and tiling-oriented capabilities.
// ------------------------------------------------------------

#include "integrations/hyprland/hyprland_window_backend.h"

#include <cassert>
#include <optional>
#include <string>
#include <vector>

namespace
{

const std::string clients = R"json([
  {
    "address": "0xabc",
    "mapped": true,
    "at": [10, 20],
    "size": [800, 600],
    "workspace": {"id": 2, "name": "2"},
    "class": "org.example.App",
    "title": "A \"quoted\" title",
    "pid": 42,
    "fullscreen": 1,
    "focusHistoryID": 0,
    "stableId": "18000001"
  },
  {
    "address": "0xdef",
    "mapped": true,
    "at": [900, 20],
    "size": [700, 500],
    "workspace": {"id": 7, "name": "special:mail"},
    "class": "mail-client",
    "title": "Mail",
    "pid": 84,
    "fullscreen": 0,
    "focusHistoryID": 3,
    "stableId": "18000002"
  },
  {"address": "0xdead", "mapped": false, "stableId": "ignored"}
])json";

const std::string monitors = R"json([
  {"id": 0, "activeWorkspace": {"id": 2, "name": "2"}},
  {"id": 1, "activeWorkspace": {"id": 9, "name": "9"}}
])json";

const std::string active = R"json({
  "address": "0xabc",
  "stableId": "18000001"
})json";

} // namespace

int main()
{
    const auto snapshot = parse_hyprland_snapshot(
        clients,
        monitors,
        active);
    assert(snapshot.windows.size() == 2);
    assert(snapshot.windows[0].id == "18000002");
    assert(snapshot.windows[1].id == "18000001");
    assert(snapshot.windows[1].caption == "A \"quoted\" title");
    assert(snapshot.windows[1].desktop_file_name == "org.example.App");
    assert(snapshot.windows[1].frame_geometry.x == 10);
    assert(snapshot.windows[1].frame_geometry.width == 800);
    assert(snapshot.windows[1].maximized);
    assert(snapshot.windows[1].on_current_desktop);
    assert(!snapshot.windows[0].on_current_desktop);
    assert(snapshot.active_window == std::optional<WindowId>("18000001"));
    assert(snapshot.stacking_order.back() == "18000001");

    std::vector<std::vector<std::string>> commands;
    HyprlandWindowBackend backend(
        [](const std::vector<std::string> &arguments)
            -> std::optional<std::string>
        {
            if (arguments == std::vector<std::string>{"version"})
                return "Hyprland 0.55.2";
            if (arguments == std::vector<std::string>{"-j", "clients"})
                return clients;
            if (arguments == std::vector<std::string>{"-j", "monitors"})
                return monitors;
            if (arguments == std::vector<std::string>{"-j", "activewindow"})
                return active;
            return std::nullopt;
        },
        [&commands](const std::vector<std::string> &arguments)
        {
            commands.push_back(arguments);
            return true;
        });

    backend.start();
    assert(backend.connected());
    assert(backend.name() == "Hyprland");
    const auto capabilities = backend.capabilities();
    assert(capabilities.can_activate);
    assert(capabilities.can_raise);
    assert(capabilities.can_close);
    assert(capabilities.can_maximize);
    assert(!capabilities.can_minimize);
    assert(capabilities.provides_virtual_desktops);
    assert(capabilities.provides_frame_geometry);
    assert(capabilities.thumbnail_policy ==
           WindowThumbnailPolicy::capture_on_demand);

    assert(backend.present_windows({"18000002", "18000001"}));
    assert(commands.back().size() == 3);
    assert(commands.back()[1] == "focuswindow");
    assert(commands.back()[2] == "address:0xabc");
    assert(backend.close_window("18000002"));
    assert(commands.back()[1] == "closewindow");
    assert(commands.back()[2] == "address:0xdef");
    assert(backend.set_window_maximized("18000001", true));
    assert(commands.back()[1] == "fullscreen");
    assert(commands.back()[2] == "1 set");
    assert(!backend.set_window_minimized("18000001", true));
    assert(!backend.hide_windows({"18000001"}));

    backend.stop();
    return 0;
}
