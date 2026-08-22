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
//
// ------------------------------------------------------------

#include "application/dock_process_application.h"

#include <gtk/gtk.h>

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

    return 0;
}
