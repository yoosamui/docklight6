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
