// ------------------------------------------------------------
// Docklight 6.0 — Shell autohide handoff regression.
// Runs the production controller and GTK window under an isolated X server.
// Models hidden placement while compositor geometry is unavailable, then
// restores Shell ownership. Also checks cancellation of queued local work.
// Private access is limited to the controller under test to stage lifecycle
// boundaries without waiting for a real desktop lock or long suspend.
// ------------------------------------------------------------

#include <gtkmm.h>
#include "dock/dock_window.h"
#include "autohide/dock_reveal_window.h"
#include "tests/fake_window_backend.h"
#include "windowing/window_registry.h"
#include "application/dock_runtime_info.h"
#define private public
#include "autohide/dock_autohide_controller.h"
#undef private
#include <cassert>
#include <iostream>

class ShellBackend : public FakeWindowBackend
{
  public:
    bool geometry_available = true;
    WindowBackendCapabilities capabilities() const override
    {
        auto c = FakeWindowBackend::capabilities();
        c.provides_dock_autohide_animation = true;
        c.provides_dock_reveal_trigger = true;
        return c;
    }
    std::optional<WindowIconGeometry> dock_surface_geometry() const override
    {
        if (!geometry_available)
            return std::nullopt;
        return WindowIconGeometry{100, 700, 800, 68};
    }
};
int main(int argc, char **argv)
{
    Gtk::Main gtk(argc, argv);
    ShellBackend backend;
    WindowRegistry registry(backend);
    registry.start();
    DockConfiguration config;
    config.layout_request.autohide = DockAutohide::none;
    DockWindow window(config,
                      Gdk::Display::get_default()->get_primary_monitor(),
                      &registry, DockRuntimeInfo{});
    DockAutohideController controller(window, 400);
    controller.m_effect = DockAutohideEffect::gnome;
    controller.m_mode = DockAutohide::autohide;
    DockPlacement placement;
    placement.anchor_bottom = true;
    placement.width = 800;
    placement.height = 68;
    controller.set_placement(placement, {100, 700});
    assert(controller.uses_shell_autohide_animation());
    controller.m_hidden = true;
    controller.m_shell_animation_active = true;
    controller.m_shell_state =
        DockAutohideController::ShellDockState::hidden;
    // Model unavailable compositor geometry while a hidden dock is resized.
    backend.geometry_available = false;
    placement.width = 840;
    controller.set_placement(placement, {80, 700});
    assert(window.get_opacity() == 0.0);
    assert(controller.m_shell_animation_active);
    backend.geometry_available = true;
    controller.request_reveal();
    std::cout << "GTK opacity after Shell reveal: " << window.get_opacity()
              << '\n';
    assert(window.get_opacity() == 1.0);
    assert(!controller.m_animation_timer.connected());
    assert(controller.m_x11_animation_tick == 0);
    assert(!controller.m_pending_x11_reveal_animation);

    // Delayed local callbacks must not make GTK transparent after handoff.
    bool stale_callback_ran = false;
    auto stale_callback = [&]()
    {
        stale_callback_ran = true;
        window.set_opacity(0.0);
        return false;
    };
    controller.m_animation_timer =
        Glib::signal_timeout().connect(stale_callback, 1);
    controller.m_x11_reveal_start_timer =
        Glib::signal_timeout().connect(stale_callback, 1);
    controller.m_pending_x11_reveal_animation = true;
    controller.m_x11_animation_tick =
        window.add_tick_callback([&](const Glib::RefPtr<Gdk::FrameClock> &)
                                 { return stale_callback(); });
    window.set_opacity(0.0);
    controller.request_shell_visibility(true);
    assert(window.get_opacity() == 1.0);
    assert(!controller.m_animation_timer.connected());
    assert(!controller.m_x11_reveal_start_timer.connected());
    assert(controller.m_x11_animation_tick == 0);
    assert(!controller.m_pending_x11_reveal_animation);
    g_usleep(5000);
    auto context = Glib::MainContext::get_default();
    while (context->iteration(false))
    {
    }
    assert(!stale_callback_ran);
    assert(window.get_opacity() == 1.0);

    // Startup placement suppression still owns its separate opacity guard.
    controller.m_initial_x11_startup_pending = true;
    controller.request_shell_visibility(true);
    assert(window.get_opacity() == 0.0);
    controller.m_initial_x11_startup_pending = false;
    controller.request_shell_visibility(false);
    assert(window.get_opacity() == 1.0);
    std::cout
        << "Shell handoff restores GTK opacity and cancels local work\n";
}
