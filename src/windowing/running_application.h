// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// running_application.h
//
// Purpose:
// Declares application groups derived by WindowRegistry from normalized
// managed windows.
//
// Responsibilities:
// - Associate a desktop-file identity with ordered window IDs.
// - Track the active member of an application group.
//
// Dependencies and ownership:
// The value owns identifiers and ordering data but does not own or control
// corresponding windows.
//
// Design notes:
// Grouping state stays independent of launcher and UI implementations.
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
