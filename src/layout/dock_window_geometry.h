// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window_geometry.h
//
// Purpose:
// Declares the realized dock surface dimensions used by geometry readers
// and placement calculations.
//
// Responsibilities:
// - Represent dock width and height.
// - Carry realization state alongside dimensions.
//
// Dependencies and ownership:
// The value owns its scalar data and no GTK resources.
//
// Design notes:
// Separating this value avoids exposing DockWindow to layout algorithms.
//
// ------------------------------------------------------------

#pragma once

struct DockWindowGeometry
{
    int x = 0;
    int y = 0;

    int width = 0;
    int height = 0;

    bool has_position = false;
};
