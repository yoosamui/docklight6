// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// muffin_window_backend.cpp
//
// Implementation overview:
// Implements Cinnamon-native activation, minimization, and restoration by
// evaluating focused scripts through Cinnamon's session D-Bus API.
//
// Important implementation decisions:
// - XIDs bridge normalized EWMH windows to Muffin MetaWindow objects.
// - Group presentation restores all members and focuses one intended target.
// - Failed native actions do not fall through to workspace-moving libwnck
//   activation.
// - Every D-Bus connection, reply, and error is released locally.
//
// ------------------------------------------------------------

#include "muffin_window_backend.h"

#include <gio/gio.h>

#include <string>

namespace
{

bool cinnamon_eval_boolean(
    const std::string &script)
{
    GError *error = nullptr;
    auto *connection =
        g_bus_get_sync(
            G_BUS_TYPE_SESSION,
            nullptr,
            &error);
    if (!connection)
    {
        g_warning(
            "Cannot connect to Cinnamon: %s",
            error ? error->message : "unknown error");
        g_clear_error(&error);
        return false;
    }

    auto *reply =
        g_dbus_connection_call_sync(
            connection,
            "org.Cinnamon",
            "/org/Cinnamon",
            "org.Cinnamon",
            "Eval",
            g_variant_new(
                "(s)",
                script.c_str()),
            G_VARIANT_TYPE("(bs)"),
            G_DBUS_CALL_FLAGS_NONE,
            1000,
            nullptr,
            &error);
    g_object_unref(connection);

    if (!reply)
    {
        g_warning(
            "Cinnamon window action failed: %s",
            error ? error->message : "unknown error");
        g_clear_error(&error);
        return false;
    }

    gboolean success = FALSE;
    const char *result = nullptr;
    g_variant_get(
        reply,
        "(b&s)",
        &success,
        &result);
    const bool accepted =
        success &&
        result &&
        g_str_equal(result, "true");
    if (!accepted)
    {
        g_warning(
            "Cinnamon did not accept window action: %s",
            result ? result : "no result");
    }

    g_variant_unref(reply);
    return accepted;
}

}

MuffinWindowBackend::MuffinWindowBackend()
    : EwmhWindowBackend("Muffin/X11")
{
}

std::optional<bool>
MuffinWindowBackend::activate_windows_override(
    const std::vector<WnckWindow *> &windows)
{
    if (windows.empty())
        return false;

    std::string script =
        "(() => { const ids = [";
    for (std::size_t index = 0;
         index < windows.size();
         ++index)
    {
        if (index > 0)
            script += ',';
        script += std::to_string(
            wnck_window_get_xid(
                windows[index]));
    }

    script +=
        "]; const wins = global.get_window_actors()"
        ".map(a => a.meta_window)"
        ".filter(w => ids.includes(w.get_xwindow()));"
        "if (wins.length === 0) return false;"
        "const target = wins[wins.length - 1];"
        "wins.forEach(w => w.unminimize());"
        "target.get_workspace().activate_with_focus("
        "target, global.get_current_time());"
        "return true; })()";

    // Never fall through to wnck_window_activate() on Muffin: it moves an
    // off-workspace window onto the current workspace.
    return cinnamon_eval_boolean(script);
}

std::optional<bool>
MuffinWindowBackend::set_window_minimized_override(
    WnckWindow *window,
    bool minimized)
{
    if (!window)
        return false;

    const std::string script =
        "(() => { const xid = " +
        std::to_string(
            wnck_window_get_xid(window)) +
        "; const actor = global.get_window_actors()"
        ".find(a => a.meta_window.get_xwindow() === xid);"
        "if (!actor) return false; actor.meta_window." +
        std::string(
            minimized
                ? "minimize"
                : "unminimize") +
        "(); return true; })()";

    return cinnamon_eval_boolean(script);
}
