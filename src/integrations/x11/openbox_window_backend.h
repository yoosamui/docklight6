// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// openbox_window_backend.h
//
// Purpose:
// Declares the LXDE/LXQt Openbox specialization of the EWMH backend.
//
// Responsibilities:
// - Advertise compositor-dependent mapped-window thumbnail behavior.
// - Hide or restore complete application groups through EWMH state requests.
// - Restore individual windows without activation or workspace movement.
//
// Dependencies and ownership:
// The class borrows WnckWindow objects from EwmhWindowBackend and uses the
// active GDK X11 display for batched state changes.
//
// Design notes:
// Group targets are resolved before dispatch so stale IDs cannot leave a
// partially transitioned application group.
//
// ------------------------------------------------------------

#pragma once

#include "ewmh_window_backend.h"

class OpenboxWindowBackend : public EwmhWindowBackend
{
public:
    OpenboxWindowBackend();

    WindowBackendCapabilities
    capabilities() const override;

    bool hide_windows(
        const std::vector<WindowId>
            &window_ids) override;

protected:
    std::optional<bool>
    set_window_minimized_override(
        WnckWindow *window,
        bool minimized) override;

private:
    static bool set_windows_hidden(
        const std::vector<WnckWindow *>
            &windows,
        bool hidden);
};
