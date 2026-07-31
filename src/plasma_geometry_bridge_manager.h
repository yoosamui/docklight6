// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// Declares the startup helper that ensures Docklight's Plasma
// geometry-bridge package is available to the current user.
//
// The helper owns no persistent state; installation files and the
// Plasma shell process own the resulting integration lifecycle.
//
// ------------------------------------------------------------

#pragma once

class PlasmaGeometryBridgeManager
{
public:
    bool ensure();
};
