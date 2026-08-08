// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_intellihide_policy.h
//
// Purpose:
// Declares the pure overlap policy used to decide whether intellihide
// should conceal the dock.
//
// Responsibilities:
// - Compare dock geometry with managed-window geometry.
// - Filter windows according to intellihide eligibility.
// - Return policy results without mutating window state.
//
// Dependencies and ownership:
// Inputs are borrowed immutable values; the policy owns no state or
// external resources.
//
// Design notes:
// Keeping the policy pure makes layout behavior independently testable.
//
// ------------------------------------------------------------

#pragma once

#include "windowing/managed_window.h"

#include <vector>

class DockIntellihidePolicy
{
public:
    static bool overlaps_dock(
        const WindowGeometry &dock,
        const std::vector<ManagedWindow>
            &windows);
};
