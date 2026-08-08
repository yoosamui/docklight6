// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// dock_intellihide_policy.cpp
//
// Implementation overview:
// Implements side-effect-free intellihide overlap filtering and rectangle
// intersection.
//
// Important implementation decisions:
// - Empty rectangles never overlap.
// - Only eligible visible windows on the relevant desktop affect the
//   result.
// - All calculations use normalized window geometry.
//
// ------------------------------------------------------------

#include "dock_intellihide_policy.h"

#include <algorithm>

namespace
{

bool intersects(
    const WindowGeometry &left,
    const WindowGeometry &right)
{
    if (left.width <= 0 ||
        left.height <= 0 ||
        right.width <= 0 ||
        right.height <= 0)
    {
        return false;
    }

    const long long left_right =
        static_cast<long long>(left.x) +
        left.width;
    const long long left_bottom =
        static_cast<long long>(left.y) +
        left.height;
    const long long right_right =
        static_cast<long long>(right.x) +
        right.width;
    const long long right_bottom =
        static_cast<long long>(right.y) +
        right.height;

    return left.x < right_right &&
           left_right > right.x &&
           left.y < right_bottom &&
           left_bottom > right.y;
}

}

bool DockIntellihidePolicy::overlaps_dock(
    const WindowGeometry &dock,
    const std::vector<ManagedWindow> &windows)
{
    return std::any_of(
        windows.begin(),
        windows.end(),
        [&dock](const ManagedWindow &window)
        {
            return !window.minimized &&
                   !window.skip_taskbar &&
                   window.on_current_desktop &&
                   intersects(
                       dock,
                       window.frame_geometry);
        });
}
