// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_layout_engine.cpp
//
// Implementation overview:
// Contains the geometry and edge-placement rules declared by
// DockLayoutEngine.
//
// Important implementation decisions:
// - Calculations use plain data structures and have no GTK effects.
// - Dock placement is expressed as anchors and compositor margins.
// - Tooltip positions use monitor coordinates and edge clamping.
// - Orientation-specific branches remain explicit at the rule boundary.
//
// ------------------------------------------------------------

#include "dock_layout_engine.h"
#include "dock_layout_metrics.h"

#include <algorithm>

// Calculates dock anchors, margins, orientation, and requested size from
// plain geometry. Keeping this outside DockWindow prevents layer-shell side
// effects from becoming mixed with placement policy.
DockPlacement
DockLayoutEngine::calculate_dock_layout(
    const DockLayoutRequest &request,
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
 
    switch (request.location)
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

    if (request.size.length > 0)
        placement.set_main_axis_size(
            request.size.length);

    if (request.size.thickness > 0)
        placement.set_cross_axis_size(
            request.size.thickness);

    if (request.autohide == DockAutohide::none)
    {
        // Let gtk-layer-shell keep the reservation synchronized with the
        // surface and its anchored edge. It includes later GTK size changes.
        placement.exclusive_zone = -1;
    }

    switch (request.alignment)
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

// Moves every anchored edge into the compositor's native work area. Main-axis
// insets keep centered/filled docks clear of panels at their ends; the
// cross-axis inset keeps the dock itself clear of a panel on its chosen edge.
void DockLayoutEngine::apply_workarea_insets(
    DockPlacement &placement,
    const MonitorGeometry &output,
    const MonitorGeometry &workarea) const
{
    const int output_right =
        output.x + output.width;
    const int output_bottom =
        output.y + output.height;
    const int workarea_right =
        workarea.x + workarea.width;
    const int workarea_bottom =
        workarea.y + workarea.height;

    if (placement.is_horizontal())
    {
        placement.margin_left +=
            std::max(0, workarea.x - output.x);
        placement.margin_right +=
            std::max(0, output_right - workarea_right);

        if (placement.anchor_top)
            placement.margin_top +=
                std::max(0, workarea.y - output.y);
        else if (placement.anchor_bottom)
            placement.margin_bottom +=
                std::max(0, output_bottom - workarea_bottom);
    }
    else
    {
        placement.margin_top +=
            std::max(0, workarea.y - output.y);
        placement.margin_bottom +=
            std::max(0, output_bottom - workarea_bottom);

        if (placement.anchor_left)
            placement.margin_left +=
                std::max(0, workarea.x - output.x);
        else if (placement.anchor_right)
            placement.margin_right +=
                std::max(0, output_right - workarea_right);
    }
}

// Calculates tooltip screen coordinates relative to the dock item and clamps
// the result to the selected monitor. The caller applies the returned values
// to the tooltip window after measurement is complete.
ScreenPosition
DockLayoutEngine::calculate_tooltip_position(
    const DockLayoutRequest &request,
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

    // Prefer the compositor's current dock coordinates. During an
    // asynchronous layer-shell relayout they can briefly differ from the
    // position implied by the requested alignment, which would leave the
    // tooltip off-centre.
    int dock_x =
        dock.has_position
            ? dock.x
            : monitor.x;

    int dock_y =
        dock.has_position
            ? dock.y
            : monitor.y;

    const bool horizontal =
        request.location == DockLocation::bottom ||
        request.location == DockLocation::top;

    if (!dock.has_position && horizontal)
    {
        if (request.alignment == DockAlignment::center)
            dock_x +=
                (monitor.width - dock_width) / 2;
        else if (request.alignment == DockAlignment::end)
            dock_x +=
                monitor.width - dock_width;
    }
    else if (!dock.has_position)
    {
        if (request.alignment == DockAlignment::center)
            dock_y +=
                (monitor.height - dock_height) / 2;
        else if (request.alignment == DockAlignment::end)
            dock_y +=
                monitor.height - dock_height;
    }

    switch (request.location)
    {
    case DockLocation::bottom:
        position.x = dock_x + item.center_x - tooltip_width / 2;
        position.y = dock_y - tooltip_height - tooltip_distance;
        break;

    case DockLocation::top:
        position.x = dock_x + item.center_x - tooltip_width / 2;
        position.y = dock_y + dock_height + tooltip_distance;
        break;

    case DockLocation::left:
        position.x = dock_x + dock_width + tooltip_distance;
        position.y =
            dock_y + item.center_y - tooltip_height / 2;
        break;

    case DockLocation::right:
        position.x = dock_x - tooltip_width - tooltip_distance;
        position.y =
            dock_y + item.center_y - tooltip_height / 2;
        break;
    }

    // A horizontal dock's first and last tooltip can cross the left or right
    // monitor edge. Move only those tooltips inward. Vertical dock tooltips
    // open beside the dock and retain their normal item-centred position.
    if (horizontal)
    {
        const int available_edge_space =
            std::max(
                0,
                monitor.width -
                    tooltip_width);

        const int edge_margin =
            std::min(
                DockLayoutMetrics::TOOLTIP_EDGE_MARGIN,
                available_edge_space / 2);

        const int minimum_x =
            monitor.x +
            edge_margin;

        const int maximum_x =
            monitor.x +
            monitor.width -
            tooltip_width -
            edge_margin;

        position.x =
            std::clamp(
                position.x,
                minimum_x,
                maximum_x);
    }
    else
    {
        const int available_edge_space =
            std::max(
                0,
                monitor.height -
                    tooltip_height);
        const int edge_margin =
            std::min(
                DockLayoutMetrics::TOOLTIP_EDGE_MARGIN,
                available_edge_space / 2);
        const int minimum_y =
            monitor.y + edge_margin;
        const int maximum_y =
            monitor.y + monitor.height -
            tooltip_height - edge_margin;

        position.y = std::clamp(
            position.y,
            minimum_y,
            maximum_y);
    }

    return position;
}
