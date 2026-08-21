// ------------------------------------------------------------
// Docklight 6.0
//
// XFCE/xfwm4 X11 backend.
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
