// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// Defines the complete, plain-data configuration snapshot shared by
// the configuration manager and dock window controller.
//
// The structure owns its settings and layout request by value. It
// contains no file-monitoring or GTK behavior.
//
// ------------------------------------------------------------

#pragma once

#include "dock_layout_types.h"
#include "dock_settings.h"

struct DockConfiguration
{
    DockSettings settings;

    DockLayoutRequest layout_request;
};
