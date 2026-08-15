// ------------------------------------------------------------
// Docklight 6.0
//
// LXDE/LXQt Openbox X11 backend.
// ------------------------------------------------------------

#pragma once

#include "ewmh_window_backend.h"

class OpenboxWindowBackend : public EwmhWindowBackend
{
public:
    OpenboxWindowBackend();

    WindowBackendCapabilities
    capabilities() const override;
};
