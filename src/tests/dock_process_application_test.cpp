// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_process_application_test.cpp
//
// Test scope:
// Guards the process lifecycle boundary against returning to GtkApplication,
// whose GTK 3 startup synchronously creates a desktop-portal session proxy.
// Verifies the exported actions dispatch without a GTK window.
//
// ------------------------------------------------------------

#include "application/dock_process_application.h"

#include <gtk/gtk.h>
#include <glibmm/main.h>

#include <array>

#include <cassert>

int main()
{
    auto application =
        DockProcessApplication::create();

    assert(application);
    assert(G_IS_APPLICATION(
        application->gobj()));
    assert(!GTK_IS_APPLICATION(
        application->gobj()));
    assert(g_strcmp0(
               g_application_get_application_id(
                   application->gobj()),
               DockProcessApplication::
                   APPLICATION_ID) == 0);

    std::array<int, 4> requests{};
    DockProcessApplication::register_actions(
        application,
        [&requests]() { ++requests[0]; },
        [&requests]() { ++requests[1]; },
        [&requests]() { ++requests[2]; },
        [&requests]() { ++requests[3]; });
    auto context = Glib::MainContext::get_default();
    const std::array<const char *, 4> names{
        "settings", "session", "about", "exit"};
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        auto action = application->lookup_action(names[i]);
        assert(action);
        assert(action->get_enabled());
        assert(!g_action_get_parameter_type(action->gobj()));
        action->activate();
        assert(requests[i] == 0);
        while (context->iteration(false)) {}
        assert(requests[i] == 1);
    }
    assert((requests == std::array<int, 4>{1, 1, 1, 1}));

    return 0;
}
