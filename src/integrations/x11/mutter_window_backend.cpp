// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// mutter_window_backend.cpp
//
// Implementation overview:
// Constructs the Mutter X11 backend with its diagnostic identity.
//
// Important implementation decisions:
// - Common Mutter X11 behavior stays on the shared EWMH implementation.
// - Optional Shell surface integration is owned by the separate hybrid class.
//
// ------------------------------------------------------------

#include "mutter_window_backend.h"

MutterWindowBackend::MutterWindowBackend()
    : EwmhWindowBackend("Mutter/X11")
{
}
