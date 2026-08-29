// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// marco_window_backend.h
//
// Purpose:
// Declares the MATE/Marco and Metacity specialization of the EWMH backend.
//
// Responsibilities:
// - Advertise the supported thumbnail caching policy.
// - Restore minimized windows without activating them.
// - Preserve each restored window's assigned workspace.
//
// Dependencies and ownership:
// The class borrows WnckWindow objects from EwmhWindowBackend and uses the
// active GDK X11 display for its restore override.
//
// Design notes:
// Only restore behavior differs; common discovery and other actions remain in
// EwmhWindowBackend.
//
// ------------------------------------------------------------

#pragma once

#include "ewmh_window_backend.h"

class MarcoWindowBackend : public EwmhWindowBackend
{
public:
    MarcoWindowBackend();

    WindowBackendCapabilities
    capabilities() const override;

protected:
    std::optional<bool>
    set_window_minimized_override(
        WnckWindow *window,
        bool minimized) override;
};
