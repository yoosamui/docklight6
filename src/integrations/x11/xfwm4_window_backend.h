// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// xfwm4_window_backend.h
//
// Purpose:
// Declares the XFCE/xfwm4 specialization of the shared EWMH backend.
//
// Responsibilities:
// - Identify the xfwm4 backend in diagnostics.
// - Restore minimized windows without activation or workspace reassignment.
//
// Dependencies and ownership:
// The class borrows WnckWindow objects from EwmhWindowBackend and uses the
// active GDK X11 display for its restore override.
//
// Design notes:
// Minimize behavior remains generic; only non-activating restore is replaced.
//
// ------------------------------------------------------------

#pragma once

#include "ewmh_window_backend.h"

class Xfwm4WindowBackend : public EwmhWindowBackend
{
public:
    Xfwm4WindowBackend();

protected:
    std::optional<bool>
    set_window_minimized_override(
        WnckWindow *window,
        bool minimized) override;
};
