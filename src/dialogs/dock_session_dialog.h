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
// Session models, application integration, and persistence belong to later
// work.
//
// ------------------------------------------------------------

#pragma once

namespace Gtk
{
class Window;
}

namespace DockSessionDialog
{
void show(
    Gtk::Window &parent);
}
