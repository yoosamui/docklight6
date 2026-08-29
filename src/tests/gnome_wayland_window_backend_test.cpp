// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// gnome_wayland_window_backend_test.cpp
//
// Purpose:
// Verifies the GNOME Wayland backend's declared capabilities and lifecycle.
//
// Responsibilities:
// - Check the GNOME-specific capability set.
// - Exercise integration registration and revisioned snapshot commit.
// - Check connection state across start and stop.
//
// Dependencies and ownership:
// The test constructs its backend on the stack and uses in-process protocol
// methods without requiring a live GNOME Shell session.
//
// Design notes:
// This is a contract-level unit test, not a real-session verification result.
//
// ------------------------------------------------------------

#include "integrations/gnome/gnome_wayland_window_backend.h"
#include "integrations/kwin/kwin_integration_protocol.h"

#include <cassert>

int main()
{
    GnomeWaylandWindowBackend backend;

    assert(backend.name() ==
           "GNOME Shell");

    const auto capabilities =
        backend.capabilities();

    assert(capabilities.can_activate);
    assert(capabilities.can_raise);
    assert(capabilities.can_close);
    assert(capabilities.can_minimize);
    assert(capabilities.can_maximize);
    assert(capabilities.provides_stacking_order);
    assert(capabilities.provides_virtual_desktops);
    assert(capabilities.provides_frame_geometry);
    assert(capabilities.provides_icons);
    assert(capabilities.provides_dock_autohide_animation);
    assert(capabilities.provides_dock_reveal_trigger);
    assert(!capabilities.provides_activities);
    assert(capabilities.accepts_icon_geometry);
    assert(!capabilities.supports_kwin_minimize_effect);

    backend.start();
    assert(backend.register_integration(
        KWinIntegrationProtocol::VERSION));
    assert(backend.begin_snapshot(1));

    ManagedWindow window;
    window.id = "gnome-window-1";
    window.desktop_file_name =
        "org.gnome.Nautilus.desktop";

    assert(backend.stage_window(1, window));
    assert(backend.commit_snapshot(
        1,
        WindowId{"gnome-window-1"},
        {"gnome-window-1"}));
    assert(backend.connected());
    assert(backend.windows().size() == 1);

    backend.stop();
    assert(!backend.connected());

    return 0;
}
