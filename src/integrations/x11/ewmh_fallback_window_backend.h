// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// ewmh_fallback_window_backend.h
//
// Purpose:
// Declares the generic backend for unrecognized EWMH-compatible X11 managers.
//
// Responsibilities:
// - Select the shared EWMH behavior without desktop-specific overrides.
// - Expose a distinct runtime name for diagnostics.
//
// Dependencies and ownership:
// All display and window resources are managed by EwmhWindowBackend.
//
// Design notes:
// Unknown managers remain isolated from assumptions made by named desktop
// specializations.
//
// ------------------------------------------------------------

#pragma once

#include "ewmh_window_backend.h"

class EwmhFallbackWindowBackend : public EwmhWindowBackend
{
public:
    EwmhFallbackWindowBackend();
};
