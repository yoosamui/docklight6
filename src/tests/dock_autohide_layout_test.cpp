// Verifies screen reservation and edge placement for autohiding docks.

#include "layout/dock_layout_engine.h"
#include "dock_intellihide_policy.h"

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
