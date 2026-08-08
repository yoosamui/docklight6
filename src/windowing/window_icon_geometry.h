// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// window_icon_geometry.h
//
// Purpose:
// Declares the screen-space rectangle published for a window's dock icon or
// the dock surface.
//
// Responsibilities:
// - Represent icon and surface geometry using plain integer coordinates.
// - Support equality checks for change suppression.
// - Cross backend, D-Bus, and Plasma bridge boundaries safely.
//
// Dependencies and ownership:
// The value owns scalar data and no compositor resource.
//
// Design notes:
// A transport-neutral value keeps geometry publication independent of GTK.
//
// ------------------------------------------------------------

#pragma once

struct WindowIconGeometry
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool operator==(
        const WindowIconGeometry &other) const
    {
        return x == other.x &&
               y == other.y &&
               width == other.width &&
               height == other.height;
    }

    bool operator!=(
        const WindowIconGeometry &other) const
    {
        return !(*this == other);
    }
};
