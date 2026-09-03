// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_session_dialog.h
//
// Purpose:
// Declares the entry point for the Sessions user interface.
//
// Responsibilities:
// - Present the Session dialog from the dock home menu.
// - Keep the editor UI independent of future session state.
//
// Dependencies and ownership:
// The function borrows its parent; dialog widgets are locally owned.
//
// Design notes:
// Sessions persist through LauncherManager, which owns docklight.data. The
// dialog borrows that store rather than reading or writing the file itself.
//
// ------------------------------------------------------------

#pragma once

#include <functional>
#include <string>

namespace Gtk
{
class Window;
}

class WindowRegistry;
class LauncherManager;
struct SessionRecord;

namespace DockSessionDialog
{
// on_sessions_changed is invoked after a Session is written, so the dock can
// rebuild its Session items while the editor is still open.
void show(
    Gtk::Window &parent,
    WindowRegistry *window_registry,
    LauncherManager &launcher_manager,
    const std::function<void(const SessionRecord &)>
        &on_sessions_changed,
    const std::string &initial_session_name = {});
}
