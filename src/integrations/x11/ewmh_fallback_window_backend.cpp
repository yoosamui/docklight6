// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// ewmh_fallback_window_backend.cpp
//
// Implementation overview:
// Constructs the generic X11 backend with its diagnostic identity.
//
// Important implementation decisions:
// - No window-manager-specific action hooks are overridden.
// - Common discovery and action behavior comes entirely from EWMH.
//
// ------------------------------------------------------------

#include "ewmh_fallback_window_backend.h"

EwmhFallbackWindowBackend::EwmhFallbackWindowBackend()
    : EwmhWindowBackend("EWMH fallback/X11")
{
}
