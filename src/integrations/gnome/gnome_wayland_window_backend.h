// ------------------------------------------------------------
// Docklight 6.0
//
// GNOME Shell publishes Mutter window state through the same versioned
// transport used by the compositor integrations.  This small specialization
// keeps the mature snapshot/command implementation while advertising only
// the capabilities GNOME actually provides.
// ------------------------------------------------------------

#pragma once

#include "integrations/kwin/kwin_window_backend.h"

class GnomeWaylandWindowBackend final :
    public KWinWindowBackend
{
public:
    explicit GnomeWaylandWindowBackend(
        bool provides_dock_reveal_trigger = true);

    std::string name() const override;

    WindowBackendCapabilities
    capabilities() const override;

private:
    bool m_provides_dock_reveal_trigger = true;
};
