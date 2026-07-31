// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// Defines the plain screen-space rectangle published for a window's
// corresponding dock icon or for the dock surface itself.
//
// The value owns no compositor resource and is safe to copy across the
// backend, D-Bus service, and geometry bridge boundaries.
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
