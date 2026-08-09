// ------------------------------------------------------------
// Docklight 6.0
//
// Cinnamon/Muffin X11 backend. Common EWMH behavior is inherited while
// Cinnamon-only window actions remain isolated in this class.
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
