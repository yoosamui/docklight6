// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// kwin_x11_window_backend.cpp
//
// Implementation overview:
// Constructs the KWin X11 backend with its diagnostic identity.
//
// Important implementation decisions:
// - Common KWin X11 behavior stays on the shared EWMH implementation.
// - No Wayland companion protocol is introduced on the native X11 path.
//
// ------------------------------------------------------------

#include "kwin_x11_window_backend.h"

KWinX11WindowBackend::KWinX11WindowBackend()
    : EwmhWindowBackend("KWin/X11")
{
}
