// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// dock_settings_dialog.h
//
// Purpose:
// Declares the entry point for presenting and persisting Docklight
// settings.
//
// Responsibilities:
// - Build controls from the current configuration.
// - Present the dialog relative to its parent.
// - Commit accepted settings through the configuration manager.
//
// Dependencies and ownership:
// The function borrows its parent and shares the icon reference; dialog
// widgets are locally owned.
//
// Design notes:
// The public interface stays independent of individual settings controls.
//
// ------------------------------------------------------------

#pragma once

#include <gdkmm/pixbuf.h>
#include <glibmm/refptr.h>

namespace Gtk
{
class Window;
}

namespace DockSettingsDialog
{
void show(
    Gtk::Window &parent,
    const Glib::RefPtr<Gdk::Pixbuf> &icon);
}
