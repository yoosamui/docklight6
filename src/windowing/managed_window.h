// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// managed_window.h
//
// Purpose:
// Declares normalized window identity, metadata, geometry, desktop
// membership, and state.
//
// Responsibilities:
// - Represent backend-independent managed windows.
// - Carry copied identity and geometry data across integration boundaries.
// - Provide comparable state for registry synchronization.
//
// Dependencies and ownership:
// Value types own their copied data and no backend, D-Bus, KWin, or GTK
// resources.
//
// Design notes:
// All concrete backends normalize native state into these declarations.
//
// ------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>
#include <vector>

using WindowId = std::string;

struct WindowGeometry
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct ManagedWindow
{
    WindowId id;

    std::string desktop_file_name;
    std::string caption;
    std::string icon_name;

    std::vector<unsigned char> icon_png;
    std::vector<std::string> activity_ids;
    std::vector<std::string> desktop_ids;
    std::vector<unsigned int> desktop_numbers;

    WindowGeometry frame_geometry;

    std::int64_t process_id = 0;

    bool active = false;
    bool minimized = false;
    bool maximized = false;
    bool skip_taskbar = false;
    bool on_current_desktop = true;
};
