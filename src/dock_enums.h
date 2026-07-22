#pragma once
#include "dock_layout_metrics.h"

enum class DockOrientation
{
    horizontal,
    vertical
};

enum class DockLocation : unsigned int
{
    bottom = 0,
    left,
    top,
    right
};

enum class DockAlignment : unsigned int
{
    center = 0,
    start,
    end,
    fill
};

enum class DockAutohide : int
{
    none = -1,
    autohide = 0,
    intellihide = 1
};

class DockPlacement
{
public:
    bool anchor_left = false;
    bool anchor_right = false;
    bool anchor_top = false;
    bool anchor_bottom = false;

    int width = -1;
    int height = -1;

    int margin_left = 0;
    int margin_right = 0;
    int margin_top = 0;
    int margin_bottom = 0;

    // gtk-layer-shell exclusive zone. A value of -1 requests an automatic
    // reservation based on the dock's anchored edge and size.
    int exclusive_zone = 0;

    DockOrientation orientation =
        DockOrientation::horizontal;

    bool is_horizontal() const
    {
        return orientation ==
               DockOrientation::horizontal;
    }

    bool is_vertical() const
    {
        return orientation ==
               DockOrientation::vertical;
    }

    // Main-axis helpers keep layout policy independent of physical edges.
    // Horizontal docks map their main axis to left/right and width; vertical
    // docks map it to top/bottom and height.
    void set_main_axis_size(int size)
    {
        if (is_horizontal())
            width = size;
        else
            height = size;
    }

    void set_cross_axis_size(int size)
    {
        if (is_horizontal())
            height = size;
        else
            width = size;
    }

    void anchor_main_axis_start()
    {
        if (is_horizontal())
            anchor_left = true;
        else
            anchor_top = true;
    }

    void anchor_main_axis_end()
    {
        if (is_horizontal())
            anchor_right = true;
        else
            anchor_bottom = true;
    }

    void fill_main_axis()
    {
        anchor_main_axis_start();
        anchor_main_axis_end();
        set_main_axis_size(-1);
    }

    int main_axis_size() const
    {
        return is_horizontal()
                   ? width
                   : height;
    }

    int cross_axis_size() const
    {
        return is_horizontal()
                   ? height
                   : width;
    }

    void set_main_axis_end_margin(int margin)
    {
        if (is_horizontal())
            margin_right = margin;
        else
            margin_bottom = margin;
    }

    void set_main_axis_start_margin(int margin)
    {
        if (is_horizontal())
            margin_left = margin;
        else
            margin_top = margin;
    }
};

// Orientation-independent requested dock size. A negative value leaves that
// dimension at its natural content size.
struct DockSize
{
    int length = -1;
    int thickness = -1;
};

// Input to DockLayoutEngine. This describes the requested dock behavior;
// it is not a placement result.
struct DockLayoutSettings
{
    DockLocation location = DockLocation::bottom;
    DockAlignment alignment = DockAlignment::center;
    DockAutohide autohide = DockAutohide::none;
    // Visual preference consumed by DockWindow and DockTooltipWindow. It is
    // separate from DockPlacement because it changes no screen geometry.
    bool rounded_corners = true;

    // -1 derives a pill radius from the current icon-size based item metric.
    // A non-negative value is an explicit user override in pixels.
    int corner_radius = 6;
    DockSize size;
};

// struct TooltipLayout
// {
//     DockLocation location = DockLocation::bottom;

//     int offset = 0;    // icon position inside dock
//     int distance = 10; // distance from dock
// };
