// ------------------------------------------------------------
// Docklight 6.0
//
// MATE/Marco and Metacity X11 backend.
// ------------------------------------------------------------

#pragma once

#include "ewmh_window_backend.h"

class MarcoWindowBackend : public EwmhWindowBackend
{
public:
    MarcoWindowBackend();

    WindowBackendCapabilities
    capabilities() const override;
};
