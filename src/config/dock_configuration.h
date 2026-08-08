// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// dock_configuration.h
//
// Purpose:
// Declares the complete plain-data configuration snapshot shared by
// configuration and dock controllers.
//
// Responsibilities:
// - Combine validated runtime settings with a layout request.
// - Provide a single value for atomic configuration updates.
//
// Dependencies and ownership:
// The structure owns both values and holds no file-monitoring or GTK
// resources.
//
// Design notes:
// Consumers receive complete snapshots instead of interpreting
// configuration text.
//
// ------------------------------------------------------------

#pragma once

#include "layout/dock_layout_types.h"
#include "dock_settings.h"

struct DockConfiguration
{
    DockSettings settings;

    DockLayoutRequest layout_request;
};
