// ------------------------------------------------------------
// Docklight 6.0
// File: gtk_module_lifetime.h
// Purpose: Protect GTK callbacks from a known external module unload bug.
// Decisions: Observe the process GTK settings without changing them. Retain
// affected code for process lifetime; own only the settings notification hook.
// ------------------------------------------------------------

#pragma once

typedef struct _GtkSettings GtkSettings;

class GtkModuleLifetime
{
public:
    // Construct after GTK initialization, before entering the main loop.
    GtkModuleLifetime();
    ~GtkModuleLifetime();

    GtkModuleLifetime(const GtkModuleLifetime &) = delete;
    GtkModuleLifetime &operator=(const GtkModuleLifetime &) = delete;

private:
    GtkSettings *m_settings = nullptr;
    unsigned long m_notify_handler = 0;
};
