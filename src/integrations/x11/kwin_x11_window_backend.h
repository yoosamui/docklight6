// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// kwin_x11_window_backend.h
//
// Purpose:
// Declares the KDE/KWin X11 specialization of the shared EWMH backend.
//
// Responsibilities:
// - Identify KWin's native X11 backend in diagnostics.
// - Reuse common EWMH discovery, actions, and XID-based preview behavior.
//
// Dependencies and ownership:
// All libwnck and X11 resources are managed by EwmhWindowBackend.
//
// Design notes:
// KWin X11 uses native EWMH/XIDs rather than the Wayland script protocol.
//
// ------------------------------------------------------------

#pragma once

#include "ewmh_window_backend.h"

class KWinX11WindowBackend : public EwmhWindowBackend
{
public:
    KWinX11WindowBackend();
};
