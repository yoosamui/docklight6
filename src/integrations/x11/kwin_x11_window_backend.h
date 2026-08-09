// ------------------------------------------------------------
// Docklight 6.0
//
// KDE/KWin X11 backend using native EWMH/XIDs through libwnck.
// ------------------------------------------------------------

#pragma once

#include "ewmh_window_backend.h"

class KWinX11WindowBackend : public EwmhWindowBackend
{
public:
    KWinX11WindowBackend();
};
