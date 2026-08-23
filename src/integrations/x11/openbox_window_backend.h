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
