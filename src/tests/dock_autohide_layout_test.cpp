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

    // Openbox publishes one root-global work area. A panel on the shorter
    // right output must not shorten the full-height primary output before
    // its monitor-scoped strut is inspected.
    const MonitorGeometry global_openbox_workarea{
        0,
        0,
        4480,
        1054};
    const auto primary_initial_workarea =
        x11_initial_monitor_workarea(
            {0, 0, 2560, 1440},
            global_openbox_workarea,
            true);
    assert(primary_initial_workarea.x == 0);
    assert(primary_initial_workarea.y == 0);
    assert(primary_initial_workarea.width == 2560);
    assert(primary_initial_workarea.height == 1440);

    const auto secondary_initial_workarea =
        x11_initial_monitor_workarea(
            {2560, 0, 1920, 1080},
            global_openbox_workarea,
            true);
    assert(secondary_initial_workarea.x == 2560);
    assert(secondary_initial_workarea.y == 0);
    assert(secondary_initial_workarea.width == 1920);
    assert(secondary_initial_workarea.height == 1080);

    const auto single_monitor_workarea =
        x11_initial_monitor_workarea(
            monitor,
            {0, 32, 1920, 1048},
            false);
    assert(single_monitor_workarea.x == 0);
    assert(single_monitor_workarea.y == 32);
    assert(single_monitor_workarea.width == 1920);
    assert(single_monitor_workarea.height == 1048);

    // Native GNOME and Plasma panels are compositor chrome rather than X11
    // dock clients. Preserve their monitor-scoped GDK work area on XWayland
    // instead of replacing it merely because two monitors exist.
    const auto gnome_xwayland_workarea =
        x11_wayland_monitor_workarea(
            {0, 0, 2560, 1440},
            {0, 29, 2560, 1411});
    assert(gnome_xwayland_workarea.x == 0);
    assert(gnome_xwayland_workarea.y == 29);
    assert(gnome_xwayland_workarea.width == 2560);
    assert(gnome_xwayland_workarea.height == 1411);

    const auto plasma_xwayland_workarea =
        x11_wayland_monitor_workarea(
            {0, 0, 2560, 1440},
            {0, 44, 2560, 1396});
    assert(plasma_xwayland_workarea.x == 0);
    assert(plasma_xwayland_workarea.y == 44);
    assert(plasma_xwayland_workarea.width == 2560);
    assert(plasma_xwayland_workarea.height == 1396);

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

    const auto top_reveal = edge_reveal_geometry(
        placement,
        monitor,
        2);
    assert(top_reveal.x == 760);
    // Cinnamon and other X11 desktops keep their panel above ordinary
    // clients. The trigger must therefore sit at the dock-facing side of the
    // panel rather than underneath it at the physical screen edge.
    assert(top_reveal.y == 32);
    assert(top_reveal.width == 400);
    assert(top_reveal.height == 2);
    assert(point_on_physical_reveal_edge(
        placement,
        monitor,
        2,
        960,
        0));
    assert(!point_on_physical_reveal_edge(
        placement,
        monitor,
        2,
        759,
        0));
    assert(!point_on_physical_reveal_edge(
        placement,
        monitor,
        2,
        960,
        2));

    DockPlacement bottom_reveal_placement = placement;
    bottom_reveal_placement.anchor_top = false;
    bottom_reveal_placement.anchor_bottom = true;
    bottom_reveal_placement.margin_top = 0;
    bottom_reveal_placement.margin_bottom = 24;
    const auto bottom_reveal = edge_reveal_geometry(
        bottom_reveal_placement,
        monitor,
        2);
    assert(bottom_reveal.y == 1054);
    assert(point_on_physical_reveal_edge(
        bottom_reveal_placement,
        monitor,
        2,
        960,
        1079));

    DockPlacement left_reveal_placement;
    left_reveal_placement.orientation =
        DockOrientation::vertical;
    left_reveal_placement.anchor_left = true;
    left_reveal_placement.anchor_top = true;
    left_reveal_placement.height = 400;
    left_reveal_placement.margin_left = 32;
    left_reveal_placement.margin_top = 340;
    const auto left_reveal = edge_reveal_geometry(
        left_reveal_placement,
        monitor,
        2);
    assert(left_reveal.x == 32);
    assert(left_reveal.y == 340);
    assert(left_reveal.width == 2);
    assert(left_reveal.height == 400);
    assert(point_on_physical_reveal_edge(
        left_reveal_placement,
        monitor,
        2,
        0,
        540));

    DockPlacement right_reveal_placement =
        left_reveal_placement;
    right_reveal_placement.anchor_left = false;
    right_reveal_placement.anchor_right = true;
    right_reveal_placement.margin_left = 0;
    right_reveal_placement.margin_right = 64;
    const auto right_reveal = edge_reveal_geometry(
        right_reveal_placement,
        monitor,
        2);
    assert(right_reveal.x == 1854);
    assert(point_on_physical_reveal_edge(
        right_reveal_placement,
        monitor,
        2,
        1919,
        540));

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

    DockPlacement top_slide;
    top_slide.orientation =
        DockOrientation::horizontal;
    top_slide.anchor_top = true;
    top_slide.margin_top = 27;
    const auto top_hidden =
        x11_hidden_screen_position(
            top_slide,
            528,
            27,
            1504,
            62);
    assert(top_hidden.x == 528);
    assert(top_hidden.y == -35);

    DockPlacement bottom_slide = top_slide;
    bottom_slide.anchor_top = false;
    bottom_slide.anchor_bottom = true;
    bottom_slide.margin_top = 0;
    bottom_slide.margin_bottom = 27;
    const auto bottom_hidden =
        x11_hidden_screen_position(
            bottom_slide,
            528,
            1351,
            1504,
            62);
    assert(bottom_hidden.x == 528);
    assert(bottom_hidden.y == 1378);

    DockPlacement left_slide;
    left_slide.orientation =
        DockOrientation::vertical;
    left_slide.anchor_left = true;
    const auto left_hidden =
        x11_hidden_screen_position(
            left_slide,
            0,
            356,
            58,
            728);
    assert(left_hidden.x == -58);
    assert(left_hidden.y == 356);

    DockPlacement right_slide = left_slide;
    right_slide.anchor_left = false;
    right_slide.anchor_right = true;
    const auto right_hidden =
        x11_hidden_screen_position(
            right_slide,
            2502,
            356,
            58,
            728);
    assert(right_hidden.x == 2560);
    assert(right_hidden.y == 356);

    const MonitorGeometry adjacent_right_monitor{
        1920,
        0,
        1920,
        1080};
    assert(horizontal_hide_corridor_intersects_monitor(
        placement,
        1856,
        356,
        64,
        400,
        adjacent_right_monitor));
    assert(!horizontal_hide_corridor_intersects_monitor(
        placement,
        1856,
        356,
        64,
        400,
        {0, 1080, 1920, 1080}));

    DockPlacement left_placement = placement;
    left_placement.anchor_right = false;
    left_placement.anchor_left = true;
    assert(horizontal_hide_corridor_intersects_monitor(
        left_placement,
        1920,
        356,
        64,
        400,
        {0, 0, 1920, 1080}));
    assert(!horizontal_hide_corridor_intersects_monitor(
        left_placement,
        1920,
        356,
        64,
        400,
        adjacent_right_monitor));

    DockPlacement right_strut;
    right_strut.orientation =
        DockOrientation::vertical;
    right_strut.anchor_right = true;

    // The primary output ends at x=2560, but the X11 root continues across
    // the second monitor to x=4480. A root-right strut here would reserve the
    // complete second monitor, so an internal-edge dock must publish none.
    assert(!x11_strut_reaches_root_edge(
        right_strut,
        2502,
        29,
        58,
        1411,
        4480,
        1440));

    assert(x11_strut_reaches_root_edge(
        right_strut,
        4422,
        29,
        58,
        1411,
        4480,
        1440));

    // Preserve a valid combined outer-edge reservation when another panel
    // contributes the margin between the dock and the root boundary.
    right_strut.margin_right = 64;
    assert(x11_strut_reaches_root_edge(
        right_strut,
        4358,
        29,
        58,
        1411,
        4480,
        1440));

    DockPlacement left_strut;
    left_strut.orientation =
        DockOrientation::vertical;
    left_strut.anchor_left = true;
    left_strut.margin_left = 32;
    assert(x11_strut_reaches_root_edge(
        left_strut,
        32,
        29,
        58,
        1411,
        4480,
        1440));

    DockPlacement top_strut;
    top_strut.orientation =
        DockOrientation::horizontal;
    top_strut.anchor_top = true;
    assert(!x11_strut_reaches_root_edge(
        top_strut,
        2800,
        164,
        400,
        64,
        4480,
        1440));

    DockPlacement bottom_strut;
    bottom_strut.orientation =
        DockOrientation::horizontal;
    bottom_strut.anchor_bottom = true;
    assert(x11_strut_reaches_root_edge(
        bottom_strut,
        1080,
        1376,
        400,
        64,
        4480,
        1440));

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

    // A fitted preview's CSS borders can make GTK's final allocation larger
    // than the requested card geometry. Bottom previews must remain centred
    // on the group icon and keep their lower edge (and therefore their gap to
    // the dock) fixed when that happens.
    const ScreenPosition requested_preview{
        300,
        500};
    const auto allocated_bottom_preview =
        overlay_position_for_allocation(
            DockLocation::bottom,
            requested_preview,
            1000,
            200,
            1012,
            208);
    assert(allocated_bottom_preview.x == 294);
    assert(allocated_bottom_preview.y == 492);
    assert(allocated_bottom_preview.y + 208 ==
        requested_preview.y + 200);

    const auto allocated_top_preview =
        overlay_position_for_allocation(
            DockLocation::top,
            requested_preview,
            1000,
            200,
            1012,
            208);
    assert(allocated_top_preview.x == 294);
    assert(allocated_top_preview.y == 500);

    return 0;
}
