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

    WindowGeometry frame_geometry;

    std::int64_t process_id = 0;

    bool active = false;
    bool minimized = false;
    bool maximized = false;
    bool skip_taskbar = false;
};
