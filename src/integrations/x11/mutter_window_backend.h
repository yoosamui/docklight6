// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// mutter_window_backend.h
//
// Purpose:
// Declares the GNOME/Mutter X11 specialization of the shared EWMH backend.
//
// Responsibilities:
// - Identify Mutter's native X11 backend in diagnostics.
// - Reuse common EWMH discovery, actions, and XComposite capture behavior.
//
// Dependencies and ownership:
// All libwnck and X11 resources are managed by EwmhWindowBackend.
//
// Design notes:
// GNOME Shell-specific dock animation is layered by GnomeX11WindowBackend,
// leaving this class responsible only for ordinary Mutter X11 behavior.
//
// ------------------------------------------------------------

#pragma once

#include "ewmh_window_backend.h"

class MutterWindowBackend : public EwmhWindowBackend
{
public:
    MutterWindowBackend();
};
