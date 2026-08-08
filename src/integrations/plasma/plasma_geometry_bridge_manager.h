// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// plasma_geometry_bridge_manager.h
//
// Purpose:
// Declares startup support for installing Docklight's Plasma
// geometry-bridge package.
//
// Responsibilities:
// - Locate packaged bridge files.
// - Ensure the per-user package is current.
// - Coordinate any required Plasma shell refresh.
//
// Dependencies and ownership:
// The helper owns no persistent state; installed files and Plasma own the
// resulting lifecycle.
//
// Design notes:
// Installation support is kept outside the runtime geometry service.
//
// ------------------------------------------------------------

#pragma once

class PlasmaGeometryBridgeManager
{
public:
    bool ensure();
};
