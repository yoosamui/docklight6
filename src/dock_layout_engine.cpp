#include "dock_layout_engine.h"

#include <algorithm>

DockPlacement
DockLayoutEngine::calculate_dock_layout(
    const DockLayoutSettings &settings,
    const MonitorGeometry &monitor,
    const DockWindowGeometry &dock) const
{
    DockPlacement placement;

    // The window supplies its natural content size. The engine owns the
    // decision to request that size from the layer-shell surface.
    if (dock.width > 0)
        placement.width = dock.width;

    if (dock.height > 0)
        placement.height = dock.height;
 
    switch (settings.location)
    {
    case DockLocation::bottom:

        placement.anchor_bottom = true;
        placement.orientation =
            DockOrientation::horizontal;

        break;

    case DockLocation::top:

        placement.anchor_top = true;
        placement.orientation =
            DockOrientation::horizontal;

        break;

    case DockLocation::left:

        placement.anchor_left = true;
        placement.orientation =
            DockOrientation::vertical;

        break;

    case DockLocation::right:

        placement.anchor_right = true;
        placement.orientation =
            DockOrientation::vertical;

        break;
    }

    if (settings.size.length > 0)
        placement.set_main_axis_size(
            settings.size.length);

    if (settings.size.thickness > 0)
        placement.set_cross_axis_size(
            settings.size.thickness);

    if (settings.autohide == DockAutohide::none)
    {
        // Let gtk-layer-shell keep the reservation synchronized with the
        // surface and its anchored edge. It includes later GTK size changes.
        placement.exclusive_zone = -1;
    }

    switch (settings.alignment)
    {
    case DockAlignment::center:
    {
        const int main_axis_size =
            placement.main_axis_size();

        const int monitor_main_axis_size =
            placement.is_horizontal()
                ? monitor.width
                : monitor.height;

        // Constrain both ends of the main axis with equal margins. Besides
        // centering the surface, this is the panel-shaped anchor layout KWin
        // needs in order to honor an exclusive zone for a partial-width (or
        // partial-height) dock.
        if (main_axis_size > 0 &&
            monitor_main_axis_size > 0)
        {
            const int margin =
                std::max(
                    0,
                    (monitor_main_axis_size - main_axis_size) / 2);

            placement.anchor_main_axis_start();
            placement.anchor_main_axis_end();
            placement.set_main_axis_start_margin(margin);
            placement.set_main_axis_end_margin(
                margin);
        }

        break;
    }

    case DockAlignment::start:
        placement.anchor_main_axis_start();
        break;

    case DockAlignment::end:
        placement.anchor_main_axis_end();
        break;

    case DockAlignment::fill:
        placement.fill_main_axis();
        break;
    }

    return placement;
}

ScreenPosition
DockLayoutEngine::calculate_tooltip_position(
    const DockLayoutSettings &settings,
    const MonitorGeometry &monitor,
    const DockWindowGeometry &dock,
    const ItemGeometry &item,
    int tooltip_width,
    int tooltip_height,
    int tooltip_distance) const
{
    ScreenPosition position;

    const int dock_width = dock.width;
    const int dock_height = dock.height;

    // With an exclusive zone, KWin positions an overlay margin from the
    // remaining work-area edge. The dock thickness is already excluded in
    // that coordinate system. Autohiding docks have no reservation, so their
    // tooltip still needs to clear the dock thickness explicitly.
    const bool dock_reserves_space =
        settings.autohide == DockAutohide::none;

    const int horizontal_edge_offset =
        (dock_reserves_space ? 0 : dock_height) +
        tooltip_distance;

    const int vertical_edge_offset =
        (dock_reserves_space ? 0 : dock_width) +
        tooltip_distance;

    int dock_x = 0;
    int dock_y = 0;

    const bool horizontal =
        settings.location == DockLocation::bottom ||
        settings.location == DockLocation::top;

    if (horizontal)
    {
        if (settings.alignment == DockAlignment::center)
            dock_x = (monitor.width - dock_width) / 2;
        else if (settings.alignment == DockAlignment::end)
            dock_x = monitor.width - dock_width;
    }
    else
    {
        if (settings.alignment == DockAlignment::center)
            dock_y = (monitor.height - dock_height) / 2;
        else if (settings.alignment == DockAlignment::end)
            dock_y = monitor.height - dock_height;
    }

    switch (settings.location)
    {
    case DockLocation::bottom:
        position.x = dock_x + item.center_x - tooltip_width / 2;
        // Bottom-anchored margins are measured from the screen edge. Clear
        // both the dock thickness and the requested tooltip distance.
        position.y = horizontal_edge_offset;
        break;

    case DockLocation::top:
        position.x = dock_x + item.center_x - tooltip_width / 2;
        position.y = horizontal_edge_offset;
        break;

    case DockLocation::left:
        // Side-anchored margins are likewise measured from the screen edge.
        position.x = vertical_edge_offset;
        // GTK item allocations use a top-origin coordinate system. Keep the
        // side tooltip in that same system so its vertical centre follows the
        // item centre directly.
        position.y =
            dock_y + item.center_y - tooltip_height / 2;
        break;

    case DockLocation::right:
        position.x = vertical_edge_offset;
        position.y =
            dock_y + item.center_y - tooltip_height / 2;
        break;
    }

    return position;
}
