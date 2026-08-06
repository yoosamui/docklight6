// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// Defines the plain width and height of the realized dock surface for
// use by geometry readers and placement calculations.
//
// This value type contains no GTK behavior and owns no resources.
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
