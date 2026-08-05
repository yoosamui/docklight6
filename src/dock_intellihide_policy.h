// ------------------------------------------------------------
// Docklight 6.0
//
// Declares the side-effect-free window-overlap policy for intellihide.
// ------------------------------------------------------------

#pragma once

#include "managed_window.h"

#include <vector>

class DockIntellihidePolicy
{
public:
    static bool overlaps_dock(
        const WindowGeometry &dock,
        const std::vector<ManagedWindow>
            &windows);
};
