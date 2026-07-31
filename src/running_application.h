// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// Defines the application grouping derived by WindowRegistry from
// normalized managed windows.
//
// The value owns its application identity and ordered window IDs; it
// does not own or control the corresponding windows.
//
// ------------------------------------------------------------

#pragma once

#include "managed_window.h"

#include <optional>
#include <string>
#include <vector>

struct RunningApplication
{
    std::string desktop_file_name;

    std::vector<WindowId> window_ids;

    std::optional<WindowId> active_window_id;
};
