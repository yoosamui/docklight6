// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// dock_layout_geometry.cpp
//
// Implementation overview:
// Converts live GTK widget and GDK monitor allocations into the plain
// coordinate structures consumed by DockLayoutEngine.
//
// Important implementation decisions:
// - Item coordinates are translated into the dock window's space.
// - Work-area and full-output geometry are exposed separately.
// - Missing native windows produce safe empty geometry values.
// - No placement policy is applied while reading geometry.
//
// ------------------------------------------------------------

#include "dock_layout_geometry.h"

#include "dock/dock_item.h"
#include "dock/dock_window.h"

#include <gdkmm/monitor.h>
#include <gdkmm/rectangle.h>
#include <gdkmm/window.h>

// Reads the item's current allocation relative to DockWindow. Returning a
// plain geometry value keeps GTK objects out of layout calculations.
ItemGeometry
DockLayoutGeometry::item_geometry(
    Gtk::Widget &item,
    DockWindow &dock) const
{
    ItemGeometry geometry;

    auto alloc = item.get_allocation();

    int x = 0;
    int y = 0;

    item.translate_coordinates(
        dock,
        0,
        0,
        x,
        y);

    geometry.x = x;
    geometry.y = y;

    geometry.width =
        alloc.get_width();

    geometry.height =
        alloc.get_height();

    geometry.center_x =
        x + geometry.width / 2;

    geometry.center_y =
        y + geometry.height / 2;

    return geometry;
}

DockWindowGeometry
DockLayoutGeometry::dock_geometry(
    DockWindow &dock) const
{
    DockWindowGeometry geometry;

    // Once mapped, use the actual layer-shell allocation. This is the size
    // that determines a tooltip's physical offset from the dock.
    if (dock.get_window())
    {
        geometry.width = dock.get_allocated_width();
        geometry.height = dock.get_allocated_height();
    }

    if (geometry.width > 0 && geometry.height > 0)
        return geometry;

    int minimum_width = 0;
    int natural_width = 0;
    int minimum_height = 0;
    int natural_height = 0;

    dock.get_preferred_width(
        minimum_width,
        natural_width);

    dock.get_preferred_height(
        minimum_height,
        natural_height);

    geometry.width = natural_width;
    geometry.height = natural_height;

    return geometry;
}

MonitorGeometry
DockLayoutGeometry::monitor_geometry(
    const Glib::RefPtr<Gdk::Monitor>
        &monitor) const
{
    MonitorGeometry geometry;

    if (!monitor)
        return geometry;

    Gdk::Rectangle monitor_workarea;

    monitor->get_workarea(monitor_workarea);

    geometry.x = monitor_workarea.get_x();
    geometry.y = monitor_workarea.get_y();
    geometry.width = monitor_workarea.get_width();
    geometry.height = monitor_workarea.get_height();

    return geometry;
}

MonitorGeometry
DockLayoutGeometry::output_geometry(
    const Glib::RefPtr<Gdk::Monitor>
        &monitor) const
{
    MonitorGeometry geometry;

    if (!monitor)
        return geometry;

    Gdk::Rectangle monitor_output;
    monitor->get_geometry(monitor_output);

    geometry.x = monitor_output.get_x();
    geometry.y = monitor_output.get_y();
    geometry.width = monitor_output.get_width();
    geometry.height = monitor_output.get_height();

    return geometry;
}
