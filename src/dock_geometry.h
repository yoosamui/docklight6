#pragma once
// ==============================================================================
// Docklight legacy DockGeometry
// ==============================================================================
// DESCRIPTION
//
// This is the previous metrics implementation, retained as a reference while
// useful measurements are migrated into the new layout architecture:
//
// DockLayoutSettings -> DockLayoutEngine -> DockPlacement
//
// New DockWindow layout code must use DockLayoutGeometry and
// DockLayoutEngine rather than this class.
//
// Geometry is automatically rebuilt whenever user settings change. Currently
// ICON_SIZE is the primary input, but the design allows future expansion for
// DPI scaling, font size, themes, animations and other layout-related settings.
//
//
// AUTHOR: yoosamui
// DATE: 2026-06-05
// ==============================================================================
#include <gtkmm/window.h>
#include "dock_enums.h"



struct PopupGeometry
{
    int x = 0;
    int y = 0;
};

struct DockGeometryData
{
    // Dock position on screen
    int dock_x = 0;
    int dock_y = 0;

    // Dock size
    int dock_width = 0;
    int dock_height = 0;

    // Item center in SCREEN coordinates
    int item_center_x = 0;
    int item_center_y = 0;

    // Popup size
    int popup_width = 0;
    int popup_height = 0;

    // Space between dock and popup
    int margin = 8;

    // Dock orientation
    DockLocation position = DockLocation::bottom;
};

class DockGeometry
{
public:
    explicit DockGeometry(Gtk::Window &window);

    static PopupGeometry calculate_popup_position(
        const DockGeometryData &geometry);

    //
    // Icon
    //

    int icon_size() const;

    //
    // Item
    //

    int item_width() const;

    int item_height() const;

    int item_spacing() const;

    //
    // Dock
    //

    int dock_width() const;

    int dock_height() const;

    void set_item_count(int item_count);

    //
    // Tooltip
    //

    int tooltip_width() const;

    int tooltip_height() const;

    int tooltip_distance() const;

    //
    // Position helpers
    //

    int dock_x() const;

    int dock_y() const;

    int icon_center_x(
        int item_x) const;

    int icon_center_y(
        int item_y) const;

    int tooltip_x(
        int icon_center_x) const;

    int tooltip_y() const;

#ifdef DEBUG
    void dump() const;
#endif

private:
    //
    // Geometry engine
    //

    void recalculate() const;

    //
    // Calculation stages
    //

    void calculate_icon() const;

    void calculate_item() const;

    void calculate_tooltip() const;

    void calculate_dock() const;

    //
    // Geometry Rules
    //

    int calculate_item_left_margin() const;

    int calculate_item_right_margin() const;

    int calculate_item_top_margin() const;

    int calculate_item_bottom_margin() const;

    int calculate_item_spacing() const;

    int calculate_item_animation_height() const;

    int calculate_item_indicator_height() const;

    int calculate_tooltip_width() const;

    int calculate_tooltip_height() const;

    int calculate_tooltip_distance() const;

    int calculate_dock_left_padding() const;

    int calculate_dock_right_padding() const;

    int calculate_dock_top_padding() const;

    int calculate_dock_bottom_padding() const;

    //
    // Monitor information
    //

    bool update_monitor_geometry() const;

private:
    Gtk::Window &m_window;

    //
    // Cached monitor
    //

    mutable int m_monitor_x = 0;

    mutable int m_monitor_y = 0;

    mutable int m_monitor_width = 0;

    mutable int m_monitor_height = 0;

    //
    // Cached user settings
    //

    mutable int m_item_count = 0;

    mutable int m_icon_size = 0; // current input

    mutable DockLocation m_location = DockLocation::bottom;

    //
    // Cached inputs
    //

    mutable int m_cached_icon_size = -1;

    mutable int m_cached_item_count = -1;

    //
    // Icon
    //

    mutable int m_icon_width = 0;

    mutable int m_icon_height = 0;

    //
    // Item reserves
    //

    mutable int m_item_margin_top = 0;

    mutable int m_item_margin_bottom = 0;

    mutable int m_item_margin_left = 0;

    mutable int m_item_margin_right = 0;

    mutable int m_item_animation_height = 0;

    mutable int m_item_indicator_height = 0;

    //
    // Derived item
    //

    mutable int m_item_width = 0;

    mutable int m_item_height = 0;

    mutable int m_item_spacing = 0;

    //
    // Derived tooltip
    //

    mutable int m_tooltip_width = 0;

    mutable int m_tooltip_height = 0;

    mutable int m_tooltip_distance = 0;

    //
    // Derived dock
    //

    mutable int m_dock_left_padding = 0;

    mutable int m_dock_right_padding = 0;

    mutable int m_dock_top_padding = 0;

    mutable int m_dock_bottom_padding = 0;

    mutable int m_dock_width = 0;

    mutable int m_dock_height = 0;
};
