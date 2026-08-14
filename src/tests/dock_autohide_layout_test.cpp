// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_autohide_layout_test.cpp
//
// Implementation overview:
// Verifies screen reservation, edge placement, and intellihide overlap
// behavior.
//
// Important implementation decisions:
// - Tests use plain geometry values without a compositor session.
// - Assertions cover visible and autohidden placement variants.
// - Policy and layout calculations are exercised together at their value
//   boundary.
//
// ------------------------------------------------------------

#include "layout/dock_layout_engine.h"
#include "autohide/dock_intellihide_policy.h"

#include <cassert>

int main()
{
    DockLayoutEngine engine;
    DockLayoutRequest request;

    const MonitorGeometry monitor{
        0,
        0,
        1920,
        1080};
    const DockWindowGeometry dock{
        0,
        0,
        400,
        64,
        false};

    request.location = DockLocation::bottom;
    request.alignment = DockAlignment::center;
    request.autohide = DockAutohide::none;

    auto placement =
        engine.calculate_dock_layout(
            request,
            monitor,
            dock);

    assert(placement.exclusive_zone == -1);
    assert(placement.anchor_bottom);
    assert(placement.anchor_left);
    assert(placement.anchor_right);
    assert(placement.margin_left == 760);
    assert(placement.margin_right == 760);

    request.autohide = DockAutohide::autohide;
    placement = engine.calculate_dock_layout(
        request,
        monitor,
        dock);

    assert(placement.exclusive_zone == 0);
    assert(placement.width == 400);
    assert(placement.height == 64);

    const MonitorGeometry top_panel_workarea{
        0,
        32,
        1920,
        1048};

    request.location = DockLocation::top;
    placement = engine.calculate_dock_layout(
        request,
        top_panel_workarea,
        dock);
    engine.apply_workarea_insets(
        placement,
        monitor,
        top_panel_workarea);
    assert(placement.margin_top == 32);
    assert(placement.margin_left == 760);
    assert(placement.margin_right == 760);
    // X11 applies these margins from the physical output edge. The top
    // target must equal the work-area edge, not add the panel inset twice.
    assert(monitor.y + placement.margin_top ==
           top_panel_workarea.y);

    const MonitorGeometry right_dock_workarea{
        0,
        32,
        1856,
        1048};

    request.location = DockLocation::left;
    placement = engine.calculate_dock_layout(
        request,
        monitor,
        dock);

    assert(placement.exclusive_zone == 0);
    assert(placement.anchor_left);
    assert(placement.anchor_top);
    assert(placement.anchor_bottom);
    assert(placement.orientation ==
           DockOrientation::vertical);

    request.location = DockLocation::right;
    placement = engine.calculate_dock_layout(
        request,
        right_dock_workarea,
        {0, 0, 64, 400, false});
    engine.apply_workarea_insets(
        placement,
        monitor,
        right_dock_workarea);
    assert(placement.margin_right == 64);
    assert(placement.margin_top == 356);
    assert(placement.margin_bottom == 324);

    const WindowGeometry dock_window{
        760,
        1016,
        400,
        64};

    ManagedWindow overlapping;
    overlapping.frame_geometry = {
        0,
        0,
        1920,
        1080};

    assert(DockIntellihidePolicy::overlaps_dock(
        dock_window,
        {overlapping}));

    overlapping.minimized = true;
    assert(!DockIntellihidePolicy::overlaps_dock(
        dock_window,
        {overlapping}));

    overlapping.minimized = false;
    overlapping.on_current_desktop = false;
    assert(!DockIntellihidePolicy::overlaps_dock(
        dock_window,
        {overlapping}));

    ManagedWindow adjacent;
    adjacent.frame_geometry = {
        0,
        0,
        760,
        1016};

    assert(!DockIntellihidePolicy::overlaps_dock(
        dock_window,
        {adjacent}));

    DockLayoutRequest preview_request;
    preview_request.location = DockLocation::left;
    preview_request.autohide = DockAutohide::autohide;

    const DockWindowGeometry vertical_dock{
        0,
        0,
        64,
        400,
        true};
    const ItemGeometry first_item{
        0,
        0,
        64,
        64,
        32,
        32};

    const auto preview_position =
        engine.calculate_tooltip_position(
            preview_request,
            monitor,
            vertical_dock,
            first_item,
            512,
            512,
            12);

    assert(preview_position.x == 76);
    assert(preview_position.y == 8);

    return 0;
}
