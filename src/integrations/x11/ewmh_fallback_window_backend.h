// ------------------------------------------------------------
// Docklight 6.0
//
// Generic fallback for EWMH-compatible X11 window managers.
// ------------------------------------------------------------

#pragma once

#include "ewmh_window_backend.h"

class EwmhFallbackWindowBackend : public EwmhWindowBackend
{
public:
    EwmhFallbackWindowBackend();
};
