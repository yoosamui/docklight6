// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// muffin_window_backend.h
//
// Purpose:
// Declares the Cinnamon/Muffin specialization of the shared EWMH backend.
//
// Responsibilities:
// - Activate complete application groups through Cinnamon.
// - Minimize and restore individual windows through native Muffin objects.
// - Retain common EWMH discovery and normalized window state.
//
// Dependencies and ownership:
// The class borrows WnckWindow objects from EwmhWindowBackend; transient
// Cinnamon D-Bus resources are created and released by the implementation.
//
// Design notes:
// Cinnamon-specific actions stay isolated because generic libwnck activation
// can move off-workspace windows to the current workspace.
//
// ------------------------------------------------------------

#pragma once

#include "ewmh_window_backend.h"

class MuffinWindowBackend : public EwmhWindowBackend
{
public:
    MuffinWindowBackend();

protected:
    std::optional<bool>
    activate_windows_override(
        const std::vector<WnckWindow *> &windows) override;

    std::optional<bool>
    set_window_minimized_override(
        WnckWindow *window,
        bool minimized) override;
};
