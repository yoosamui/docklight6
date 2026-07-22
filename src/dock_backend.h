#pragma once

#include "dock_enums.h"

class IDockBackend
{
public:
    virtual ~IDockBackend() = default;

    virtual void initialize() = 0;

    // Backends render a completed engine result. They do not make layout
    // decisions from individual location, alignment, or autohide values.
    virtual void apply_placement(
        const DockPlacement &placement) = 0;
};
