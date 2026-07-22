// ==============================================================================
// Docklight legacy DockGeometry
// ==============================================================================
//
// DESCRIPTION
//
// This is the previous metrics implementation. It is retained as reference
// material while useful metrics are migrated into the new layout architecture.
// New DockWindow layout code must use DockLayoutGeometry and
// DockLayoutEngine instead.
//
// It calculates:
//
//   • Icon geometry
//   • Dock item geometry
//   • Tooltip geometry
//   • Dock geometry
//   • Position calculations
//
// AUTHOR: yoosamui
// DATE: 2026-06-05
// ==============================================================================
#include "dock_geometry.h"

#include "dock_settings.h"

#include <algorithm>
#include <iostream>

#include <gdkmm/display.h>
#include <gdkmm/monitor.h>
#include <gdkmm/rectangle.h>
#include <gdkmm/window.h>

PopupGeometry DockGeometry::calculate_popup_position(
    const DockGeometryData &g)
{
    PopupGeometry popup;

    switch (g.position)
    {
    case DockLocation::top:

        popup.x =
            g.item_center_x -
            g.popup_width / 2;

        popup.y =
            g.dock_y +
            g.dock_height +
            g.margin;

        break;

    case DockLocation::bottom:

        popup.x =
            g.item_center_x -
            g.popup_width / 2;

        popup.y =
            g.dock_y -
            g.popup_height -
            g.margin;

        break;

    case DockLocation::left:

        popup.x =
            g.dock_x +
            g.dock_width +
            g.margin;

        popup.y =
            g.item_center_y -
            g.popup_height / 2;

        break;

    case DockLocation::right:

        popup.x =
            g.dock_x -
            g.popup_width -
            g.margin;

        popup.y =
            g.item_center_y -
            g.popup_height / 2;

        break;
    }

    return popup;
}

//==============================================================================
//
// Constructor
//
//==============================================================================

DockGeometry::DockGeometry(Gtk::Window &window)
    : m_window(window)
{
    recalculate();
}

//==============================================================================
//
// Geometry Engine
//
//==============================================================================

void DockGeometry::recalculate() const
{
    //
    // Monitor geometry is independent of the layout cache.
    // Always refresh it.
    //

    update_monitor_geometry();

    const int icon_size =
        g_settings.icon_size();

    //
    // Geometry cache still valid.
    //

    if (icon_size == m_cached_icon_size &&
        m_item_count == m_cached_item_count)
    {
        return;
    }

    //
    // Update cache.
    //

    m_cached_icon_size =
        icon_size;

    m_cached_item_count =
        m_item_count;

    //
    // Rebuild geometry.
    //

    calculate_icon();

    calculate_item();

    calculate_tooltip();

    calculate_dock();

    std::cout
        << "recalculate(): "
        << "icon=" << icon_size
        << " cached=" << m_cached_icon_size
        << " items=" << m_item_count
        << " cached_items=" << m_cached_item_count
        << std::endl;
}

//==============================================================================
//
// Monitor Geometry
//
//==============================================================================
bool DockGeometry::update_monitor_geometry() const
{
    auto gdk_window =
        m_window.get_window();

    if (!gdk_window)
    {
        return false;
    }

    auto display =
        gdk_window->get_display();

    if (!display)
    {
        return false;
    }

    auto monitor =
        display->get_monitor_at_window(
            gdk_window);

    if (!monitor)
    {
        return false;
    }

    Gdk::Rectangle geometry;

    monitor->get_geometry(
        geometry);

    m_monitor_x =
        geometry.get_x();

    m_monitor_y =
        geometry.get_y();

    m_monitor_width =
        geometry.get_width();

    m_monitor_height =
        geometry.get_height();

    std::cout
        << "update_monitor_geometry() = "
        << "ok"
        << std::endl;

    std::cout
        << "Monitor "
        << m_monitor_x << ","
        << m_monitor_y << " "
        << m_monitor_width << "x"
        << m_monitor_height
        << std::endl;

    return true;
}

//==============================================================================
//
// Icon Geometry
//
//==============================================================================

void DockGeometry::calculate_icon() const
{

    m_icon_width =
        m_cached_icon_size;

    m_icon_height =
        m_cached_icon_size;
}

void DockGeometry::set_item_count(
    int item_count)
{
    if (m_item_count == item_count)
    {
        return;
    }

    m_item_count = item_count;

    recalculate();
}

// void DockGeometry::set_location(DockLocation location)
// {
//     if (m_location == location)
//         return;

//     m_location = location;

//     // Force recalculation
//     m_icon_size = -1;
// }

//==============================================================================
//
// Item Geometry
//
//==============================================================================

void DockGeometry::calculate_item() const
{
    //
    // Calculate all item metrics first.
    //

    const int left_margin =
        calculate_item_left_margin();

    const int right_margin =
        calculate_item_right_margin();

    const int top_margin =
        calculate_item_top_margin();

    const int bottom_margin =
        calculate_item_bottom_margin();

    const int animation_height =
        calculate_item_animation_height();

    const int indicator_height =
        calculate_item_indicator_height();

    const int spacing =
        calculate_item_spacing();

    //
    // Calculate derived item geometry.
    //

    const int width =
        left_margin + m_icon_width + right_margin;

    const int height =
        top_margin + animation_height + m_icon_height + indicator_height + bottom_margin;

    //
    // Commit the new geometry.
    //

    m_item_margin_left =
        left_margin;

    m_item_margin_right =
        right_margin;

    m_item_margin_top =
        top_margin;

    m_item_margin_bottom =
        bottom_margin;

    m_item_animation_height =
        animation_height;

    m_item_indicator_height =
        indicator_height;

    m_item_spacing =
        spacing;

    m_item_width =
        width;

    m_item_height =
        height;
}

//==============================================================================
//
// Item  Rules
//
//==============================================================================

int DockGeometry::calculate_item_left_margin() const
{
    return 8;
}

int DockGeometry::calculate_item_right_margin() const
{
    return 8;
}

int DockGeometry::calculate_item_top_margin() const
{
    return 4;
}

int DockGeometry::calculate_item_bottom_margin() const
{
    return 4;
}

int DockGeometry::calculate_item_animation_height() const
{
    //
    // Reserved for future zoom / bounce animations.
    //

    return 8;
}

int DockGeometry::calculate_item_indicator_height() const
{
    //
    // Reserved for the running application indicator.
    //

    return 6;
}

int DockGeometry::calculate_item_spacing() const
{
    return 10;
}

//==============================================================================
//
// Tooltip Geometry
//
//==============================================================================

void DockGeometry::calculate_tooltip() const
{
    //
    // Calculate tooltip dimensions.
    //

    const int width =
        calculate_tooltip_width();

    const int height =
        calculate_tooltip_height();

    const int distance =
        calculate_tooltip_distance();

    //
    // Commit calculated values.
    //

    m_tooltip_width =
        width;

    m_tooltip_height =
        height;

    m_tooltip_distance =
        distance;
}

//==============================================================================
//
// Tooltip Rules
//
//==============================================================================

int DockGeometry::calculate_tooltip_width() const
{
    //
    // Tooltip width can later depend on:
    //
    //   - font size
    //   - text length
    //   - scaling factor
    //
    // Keep the rule here.
    //

    return 220;
}

int DockGeometry::calculate_tooltip_height() const
{
    //
    // Tooltip height can later depend on:
    //
    //   - font metrics
    //   - number of lines
    //
    return 80;
}

int DockGeometry::calculate_tooltip_distance() const
{
    //
    // Distance between dock and tooltip.
    //
    // This keeps the offset out of UI code.
    //

    return 8;
}

//==============================================================================
//
// Dock Geometry
//
//==============================================================================

void DockGeometry::calculate_dock() const
{
    //
    // Calculate dock padding.
    //

    m_dock_left_padding =
        calculate_dock_left_padding();

    m_dock_right_padding =
        calculate_dock_right_padding();

    m_dock_top_padding =
        calculate_dock_top_padding();

    m_dock_bottom_padding =
        calculate_dock_bottom_padding();

    //
    // Calculate dock width.
    //
    // Formula:
    //
    // left padding
    // + items
    // + spacing between items
    // + right padding
    //

    if (m_item_count <= 0)
    {
        m_dock_width =
            m_dock_left_padding +
            m_dock_right_padding;
    }
    else
    {
        m_dock_width =
            m_dock_left_padding +
            (m_item_count * m_item_width) +
            ((m_item_count - 1) * m_item_spacing) +
            m_dock_right_padding;
    }

    //
    // Calculate dock height.
    //

    m_dock_height =
        m_dock_top_padding +
        m_item_height +
        m_dock_bottom_padding;
}

//==============================================================================
//
// Dock Rules
//
//==============================================================================

int DockGeometry::calculate_dock_left_padding() const
{
    return 8;
}

int DockGeometry::calculate_dock_right_padding() const
{
    return 8;
}

int DockGeometry::calculate_dock_top_padding() const
{
    return 4;
}

int DockGeometry::calculate_dock_bottom_padding() const
{
    return 4;
}

//==============================================================================
//
// Dock Width
//
//==============================================================================
int DockGeometry::dock_width() const
{
    recalculate();

    return m_dock_width;
}

//==============================================================================
//
// Public Getters
//
//==============================================================================

int DockGeometry::icon_size() const
{
    recalculate();

    return m_cached_icon_size;
}

int DockGeometry::item_width() const
{
    recalculate();

    return m_item_width;
}

int DockGeometry::item_height() const
{
    recalculate();

    return m_item_height;
}

int DockGeometry::item_spacing() const
{
    recalculate();

    return m_item_spacing;
}

int DockGeometry::dock_height() const
{
    recalculate();

    return m_dock_height;
}

// //==============================================================================
// //
// // Icon Position Helpers
// //
// //==============================================================================

// int DockGeometry::icon_center_x(
//     int item_x) const
// {
//     recalculate();

//     return item_x +
//            m_item_margin_left +
//            (m_icon_width / 2);
// }

// int DockGeometry::icon_center_y(
//     int item_y) const
// {
//     recalculate();

//     return item_y +
//            m_item_margin_top +
//            m_item_animation_height +
//            (m_icon_height / 2);
// }

//==============================================================================
//
// Tooltip Position Helpers
//
//==============================================================================

int DockGeometry::tooltip_x(
    int icon_center_x) const
{
    recalculate();

    return icon_center_x -
           (m_tooltip_width / 2);
}

int DockGeometry::tooltip_y() const
{
    recalculate();

    return dock_y() -
           m_tooltip_distance -
           m_tooltip_height;
}

int DockGeometry::tooltip_width() const
{
    recalculate();

    return m_tooltip_width;
}

int DockGeometry::tooltip_height() const
{
    recalculate();

    return m_tooltip_height;
}

int DockGeometry::tooltip_distance() const
{
    recalculate();

    return m_tooltip_distance;
}

//==============================================================================
//
// Dock Position
//
//==============================================================================

int DockGeometry::dock_x() const
{
    recalculate();

    std::cout
        << "Monitor "
        << m_monitor_x << ","
        << m_monitor_y << " "
        << m_monitor_width << "x"
        << m_monitor_height
        << std::endl;

    std::cout
        << "Dock width="
        << m_dock_width
        << " height="
        << m_dock_height
        << std::endl;

    switch (m_location)
    {
    case DockLocation::left:

        return m_monitor_x;

    case DockLocation::right:

        return m_monitor_x +
               m_monitor_width -
               m_dock_width;

    case DockLocation::top:
    case DockLocation::bottom:

        return m_monitor_x +
               (m_monitor_width - m_dock_width) / 2;
    }

    return 0;
}

int DockGeometry::dock_y() const
{
    recalculate();

    switch (m_location)
    {
    case DockLocation::top:

        return m_monitor_y;

    case DockLocation::bottom:

        return m_monitor_y +
               m_monitor_height -
               m_dock_height;

    case DockLocation::left:
    case DockLocation::right:

        return m_monitor_y +
               (m_monitor_height - m_dock_height) / 2;
    }

    return 0;
}

//==============================================================================
//
// Icon Position Helpers
//
//==============================================================================

int DockGeometry::icon_center_x(
    int item_x) const
{
    recalculate();

    return item_x +
           m_item_margin_left +
           (m_icon_width / 2);
}

int DockGeometry::icon_center_y(
    int item_y) const
{
    recalculate();

    return item_y +
           m_item_margin_top +
           m_item_animation_height +
           (m_icon_height / 2);
}

#ifdef DEBUG

//==============================================================================
//
// Debug Dump
//
//==============================================================================

void DockGeometry::dump() const
{
    recalculate();

    std::cout
        << "\n========== DockGeometry ==========\n";

    std::cout
        << "Monitor:\n"
        << "  x      : "
        << m_monitor_x
        << "\n"
        << "  y      : "
        << m_monitor_y
        << "\n"
        << "  width  : "
        << m_monitor_width
        << "\n"
        << "  height : "
        << m_monitor_height
        << "\n";

    std::cout
        << "\nIcon:\n"
        << "  size   : "
        << m_icon_size
        << "\n"
        << "  width  : "
        << m_icon_width
        << "\n"
        << "  height : "
        << m_icon_height
        << "\n";

    std::cout
        << "\nItem:\n"
        << "  width       : "
        << m_item_width
        << "\n"
        << "  height      : "
        << m_item_height
        << "\n"
        << "  spacing     : "
        << m_item_spacing
        << "\n"
        << "  margin left : "
        << m_item_margin_left
        << "\n"
        << "  margin right: "
        << m_item_margin_right
        << "\n"
        << "  margin top  : "
        << m_item_margin_top
        << "\n"
        << "  margin bottom: "
        << m_item_margin_bottom
        << "\n"
        << "  animation   : "
        << m_item_animation_height
        << "\n"
        << "  indicator   : "
        << m_item_indicator_height
        << "\n";

    std::cout
        << "\nTooltip:\n"
        << "  width    : "
        << m_tooltip_width
        << "\n"
        << "  height   : "
        << m_tooltip_height
        << "\n"
        << "  distance : "
        << m_tooltip_distance
        << "\n";

    std::cout
        << "\nDock:\n"
        << "  items          : "
        << m_item_count
        << "\n"
        << "  left padding   : "
        << m_dock_left_padding
        << "\n"
        << "  right padding  : "
        << m_dock_right_padding
        << "\n"
        << "  top padding    : "
        << m_dock_top_padding
        << "\n"
        << "  bottom padding : "
        << m_dock_bottom_padding
        << "\n"
        << "  width          : "
        << m_dock_width
        << "\n"
        << "  height         : "
        << m_dock_height
        << "\n";

    std::cout
        << "==================================\n"
        << std::endl;
}

#endif
