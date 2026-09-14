// ------------------------------------------------------------
// Docklight 6.0
// File: gtk_module_lifetime_test.cpp
// Purpose: Exercise real GTK module unload/theme notifications that crashed
// Docklight, using the installed KDE module in an isolated display process.
// Decisions: Skip without GTK/display or the optional module. Default tests
// late module loading; --startup tests a module loaded before the guard.
// No desktop settings are written. Assertions remain active in release tests.
// ------------------------------------------------------------

#include "presentation/gtk_module_lifetime.h"

#include <gtk/gtk.h>
#include <link.h>
#include <cstring>
#include <memory>

namespace
{
int find_module(dl_phdr_info *info, std::size_t, void *data)
{
    const char *name = std::strrchr(info->dlpi_name, '/');
    name = name ? name + 1 : info->dlpi_name;
    if (std::strcmp(name, "libwindow-decorations-gtk-module.so") == 0)
        *static_cast<bool *>(data) = true;
    return 0;
}
}

int main(int argc, char **argv)
{
    const bool startup = argc > 1 &&
        std::strcmp(argv[1], "--startup") == 0;
    if (!gtk_init_check(nullptr, nullptr))
        return 77;

    auto *settings = gtk_settings_get_default();
    std::unique_ptr<GtkModuleLifetime> guard;
    if (!startup)
        guard = std::make_unique<GtkModuleLifetime>();

    g_object_set(settings, "gtk-modules",
        "window-decorations-gtk-module", nullptr);
    bool loaded = false;
    dl_iterate_phdr(find_module, &loaded);
    if (!loaded)
        return 77;

    if (startup)
        guard = std::make_unique<GtkModuleLifetime>();

    for (int iteration = 0; iteration < 3; ++iteration)
    {
        g_object_set(settings, "gtk-modules", "", nullptr);
        g_object_notify(G_OBJECT(settings), "gtk-theme-name");
        g_object_set(settings, "gtk-theme-name",
            iteration % 2 ? "Adwaita" : "Adwaita-dark", nullptr);
        gchar *modules = nullptr;
        g_object_get(settings, "gtk-modules", &modules, nullptr);
        g_assert_cmpstr(modules, ==, "");
        g_free(modules);
        g_object_set(settings, "gtk-modules",
            "window-decorations-gtk-module", nullptr);
    }
    guard.reset();
    g_object_set(settings, "gtk-modules", "", nullptr);
    g_object_notify(G_OBJECT(settings), "gtk-theme-name");
    g_print("GTK module unload/theme notifications survived\n");
}
