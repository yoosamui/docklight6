// ------------------------------------------------------------
// Docklight 6.0
//
// KDE/KWin X11 backend. KWin's script protocol supplies stable internal IDs,
// authoritative state, stacking order, and commands on X11 as well as Wayland.
// ------------------------------------------------------------

#pragma once

#include "integrations/kwin/kwin_window_backend.h"

class KWinX11WindowBackend : public KWinWindowBackend
{
public:
    std::string name() const override;
};
