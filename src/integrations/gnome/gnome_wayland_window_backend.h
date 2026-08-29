// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// gnome_wayland_window_backend.h
//
// Purpose:
// Declares the GNOME Shell specialization of the revisioned Shell backend.
//
// Responsibilities:
// - Identify the backend as GNOME Shell.
// - Advertise only capabilities supplied by the GNOME integration.
// - Reuse revisioned snapshot and command transport behavior.
//
// Dependencies and ownership:
// The class inherits KWinWindowBackend's in-process snapshot and command
// state; the surrounding integration service owns D-Bus transport lifetime.
//
// Design notes:
// GNOME Shell shares the versioned transport while retaining a distinct and
// deliberately narrower capability declaration.
//
// ------------------------------------------------------------

#pragma once

#include "integrations/kwin/kwin_window_backend.h"

class GnomeWaylandWindowBackend final :
    public KWinWindowBackend
{
public:
    std::string name() const override;

    WindowBackendCapabilities
    capabilities() const override;
};
