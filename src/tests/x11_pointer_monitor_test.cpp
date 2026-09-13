// ------------------------------------------------------------
// Docklight 6.0
// File: x11_pointer_monitor_test.cpp
// Purpose: Exercise XI2 motion delivery, idle silence and monitor teardown.
// Decisions: Relaunch under Xvfb before injecting input, so the user's pointer
// is never touched. XTest is loaded only by this test; missing tools skip it.
// ------------------------------------------------------------
#include "presentation/x11_pointer_monitor.h"

#include <gtkmm/main.h>
#include <glib.h>
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <sys/wait.h>

#include <cassert>
#include <cstring>

namespace
{
void settle()
{
    const auto until = g_get_monotonic_time() + 150000;
    do
    {
        while (g_main_context_iteration(nullptr, false)) {}
        g_usleep(1000);
    } while (g_get_monotonic_time() < until);
}
}

int main(int argc, char **argv)
{
    if (argc != 2 || std::strcmp(argv[1], "--isolated") != 0)
    {
        auto *runner = g_find_program_in_path("xvfb-run");
        if (!runner)
            return 77;
        auto *self = g_canonicalize_filename(argv[0], nullptr);
        char *command[] = {runner, const_cast<char *>("-a"), self,
                           const_cast<char *>("--isolated"), nullptr};
        int status = 0;
        const bool launched = g_spawn_sync(
            nullptr, command, nullptr, G_SPAWN_DEFAULT,
            nullptr, nullptr, nullptr, nullptr, &status, nullptr);
        g_free(self);
        g_free(runner);
        return launched && WIFEXITED(status) ? WEXITSTATUS(status) : 1;
    }

    g_setenv("GDK_BACKEND", "x11", true);
    g_setenv("XDG_SESSION_TYPE", "x11", true);
    g_unsetenv("WAYLAND_DISPLAY");
    g_unsetenv("DOCKLIGHT_XWAYLAND_PRESENTATION");
    Gtk::Main gtk(argc, argv);
    auto *xtst = dlopen("libXtst.so.6", RTLD_NOW);
    if (!xtst)
        return 77;
    using InjectMotion = int (*)(Display *, int, int, unsigned long);
    const auto inject = reinterpret_cast<InjectMotion>(
        dlsym(xtst, "XTestFakeRelativeMotionEvent"));
    assert(inject);
    auto *display = XOpenDisplay(nullptr);
    assert(display);
    const auto move = [&]()
    {
        inject(display, 1, 0, 0);
        XFlush(display);
        settle();
    };

    int calls = 0;
    X11PointerMonitor monitor;
    assert(monitor.start([&]() { ++calls; }));
    settle();
    assert(calls == 0); // No polling callback while the pointer is stationary.
    move();
    assert(calls > 0);
    const auto moved = calls;
    settle();
    assert(calls == moved);
    monitor.stop();
    move();
    assert(calls == moved);
    assert(monitor.start([&]() { ++calls; monitor.stop(); }));
    settle();
    move();
    assert(calls == moved + 1); // Owner can stop synchronously when revealing.
    move();
    assert(calls == moved + 1);
    {
        X11PointerMonitor temporary;
        assert(temporary.start([&]() { ++calls; }));
        settle();
    }
    move();
    assert(calls == moved + 1);
    g_setenv("DOCKLIGHT_XWAYLAND_PRESENTATION", "1", true);
    assert(!monitor.start([&]() { ++calls; }));
    XCloseDisplay(display);
    dlclose(xtst);
    return 0;
}
