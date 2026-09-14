// ------------------------------------------------------------
// Docklight 6.0
// File: gtk_module_lifetime.cpp
// Purpose: Keep KDE's GTK window-decoration callback code mapped.
// Responsibilities: Retain an already-loaded affected module at startup and
// after GTK module-list changes, without enabling modules or changing themes.
// Decisions: The installed KDE module leaves notify::gtk-theme-name callbacks
// behind when unloaded. RTLD_NODELETE preserves their code and static state.
// RTLD_NOLOAD prevents this workaround from loading an unused module. This
// belongs to GTK presentation, independently of the window-manager backend.
// ------------------------------------------------------------

#include "gtk_module_lifetime.h"

#include <gtk/gtk.h>
#include <dlfcn.h>
#include <link.h>

#include <cstring>
#include <string>
#include <vector>

namespace
{

int collect_affected_module(
    dl_phdr_info *info,
    std::size_t,
    void *data)
{
    const char *path = info->dlpi_name;
    const char *basename = std::strrchr(path, '/');
    basename = basename ? basename + 1 : path;

    if (std::strcmp(
            basename,
            "libwindow-decorations-gtk-module.so") == 0)
    {
        static_cast<std::vector<std::string> *>(data)
            ->emplace_back(path);
    }
    return 0;
}

void retain_loaded_module()
{
    std::vector<std::string> paths;
    dl_iterate_phdr(collect_affected_module, &paths);

    // Finish enumeration before changing dynamic-loader reference counts.
    for (const auto &path : paths)
    {
        void *module = dlopen(
            path.c_str(),
            RTLD_LAZY | RTLD_NOLOAD | RTLD_NODELETE);
        if (module)
            dlclose(module);
    }
}

void modules_changed(GObject *, GParamSpec *, gpointer)
{
    retain_loaded_module();
}

}

GtkModuleLifetime::GtkModuleLifetime()
    : m_settings(gtk_settings_get_default())
{
    if (!m_settings)
        return;

    g_object_ref(m_settings);
    retain_loaded_module();
    // GTK's normal notify handler loads newly selected modules first.
    m_notify_handler = g_signal_connect_after(
        m_settings,
        "notify::gtk-modules",
        G_CALLBACK(modules_changed),
        nullptr);
}

GtkModuleLifetime::~GtkModuleLifetime()
{
    if (!m_settings)
        return;

    g_signal_handler_disconnect(m_settings, m_notify_handler);
    g_object_unref(m_settings);
    // NODELETE intentionally outlives this observer and GTK teardown.
}
