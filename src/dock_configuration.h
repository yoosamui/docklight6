#pragma once

#include "dock_layout_types.h"
#include "dock_settings.h"

struct DockConfiguration
{
    DockSettings settings;
    DockLayoutRequest layout_request;
};
