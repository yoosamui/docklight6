// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_layout_types.h
//
// Purpose:
// Declares the plain orientation, location, request, monitor, item, and
// placement types shared by layout components.
//
// Responsibilities:
// - Represent layout inputs and calculated placement.
// - Provide stable enums at configuration and UI boundaries.
// - Keep geometry data independent of GTK types.
//
// Dependencies and ownership:
// All declarations are value types and own no external resources.
//
// Design notes:
// These types form the data boundary around DockLayoutEngine.
//
// ------------------------------------------------------------

#pragma once

#include <algorithm>

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

enum class DockHoverEffect : unsigned int
{
    standard = 0,
    zoom = 1,
    blur = 2
};

enum class DockIndicator : unsigned int
{
    lines = 0,
    dots = 1
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
// it is not a placement result or the application's settings store.
struct DockLayoutRequest
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

struct ScreenPosition
{
    int x = 0;
    int y = 0;
};

// GTK can allocate a decorated overlay a few pixels larger than the size
// used to calculate its initial position. Keep the surface centred on its
// dock item along the main axis and preserve the edge facing the dock along
// the cross axis.
inline ScreenPosition overlay_position_for_allocation(
    DockLocation location,
    const ScreenPosition &requested_position,
    int requested_width,
    int requested_height,
    int allocated_width,
    int allocated_height)
{
    ScreenPosition position = requested_position;

    if (location == DockLocation::bottom ||
        location == DockLocation::top)
    {
        position.x +=
            (requested_width - allocated_width) / 2;
    }
    else
    {
        position.y +=
            (requested_height - allocated_height) / 2;
    }

    if (location == DockLocation::bottom)
    {
        position.y +=
            requested_height - allocated_height;
    }
    else if (location == DockLocation::right)
    {
        position.x +=
            requested_width - allocated_width;
    }

    return position;
}

// Native X11 autohide moves outward from the dock's shown position. Preserve
// the standard bottom-edge inset distance while the other edge paths use the
// complete dock thickness or their dedicated clipped animation.
inline ScreenPosition x11_hidden_screen_position(
    const DockPlacement &placement,
    int shown_x,
    int shown_y,
    int width,
    int height)
{
    ScreenPosition hidden{shown_x, shown_y};
    const int dock_width = width > 0 ? width : 1;
    const int dock_height = height > 0 ? height : 1;

    if (placement.is_horizontal())
    {
        if (placement.anchor_top)
            hidden.y -= dock_height;
        else if (placement.anchor_bottom)
            hidden.y += placement.margin_bottom > 0
                ? std::min(
                      dock_height,
                      placement.margin_bottom)
                : dock_height;
    }
    else if (placement.anchor_left)
    {
        hidden.x -= dock_width;
    }
    else if (placement.anchor_right)
    {
        hidden.x += dock_width;
    }

    return hidden;
}

struct MonitorGeometry
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// _NET_WORKAREA is one root-window rectangle per desktop, not one rectangle
// per monitor. On a multi-monitor X11 desktop, a panel on one output can
// therefore shrink the work area reported for every output. Begin with the
// selected output in that case; the caller can then apply each client's
// _NET_WM_STRUT_PARTIAL only when its range intersects this output.
inline MonitorGeometry x11_initial_monitor_workarea(
    const MonitorGeometry &output,
    const MonitorGeometry &root_workarea,
    bool multiple_monitors)
{
    if (multiple_monitors ||
        root_workarea.width <= 0 ||
        root_workarea.height <= 0)
    {
        return output;
    }

    const int left = std::max(output.x, root_workarea.x);
    const int top = std::max(output.y, root_workarea.y);
    const int right = std::min(
        output.x + output.width,
        root_workarea.x + root_workarea.width);
    const int bottom = std::min(
        output.y + output.height,
        root_workarea.y + root_workarea.height);

    if (right <= left || bottom <= top)
        return output;

    return {left, top, right - left, bottom - top};
}

// Some compositors expose an authoritative monitor-scoped work area through
// GDK. This includes Wayland compositors serving an XWayland client and
// Cinnamon/Muffin publishing _GTK_WORKAREAS_Dn on X11. Native shell panels are
// not X11 dock clients, so preserve this value on a multi-monitor session.
inline MonitorGeometry x11_scoped_monitor_workarea(
    const MonitorGeometry &output,
    const MonitorGeometry &monitor_workarea)
{
    return x11_initial_monitor_workarea(
        output,
        monitor_workarea,
        false);
}

// Place an ordinary toplevel reveal trigger beside the dock's shown edge.
// On X11, desktop panels can own the physical output edge above every client
// window. A trigger left underneath such a panel is mapped but can never
// receive pointer input, so include the dock's cross-axis panel inset. A zero
// inset still places the trigger directly on the physical output edge.
inline MonitorGeometry edge_reveal_geometry(
    const DockPlacement &placement,
    const MonitorGeometry &monitor,
    int reveal_size)
{
    const int thickness = reveal_size > 0
        ? reveal_size
        : 1;
    MonitorGeometry geometry{
        monitor.x,
        monitor.y,
        thickness,
        thickness};

    if (placement.is_horizontal())
    {
        geometry.width = placement.width > 0
            ? placement.width
            : monitor.width -
                  placement.margin_left -
                  placement.margin_right;
        if (geometry.width < 1)
            geometry.width = 1;

        if (placement.anchor_left)
            geometry.x += placement.margin_left;
        else if (placement.anchor_right)
            geometry.x += monitor.width -
                placement.margin_right - geometry.width;
        else
            geometry.x +=
                (monitor.width - geometry.width) / 2;

        if (placement.anchor_top)
            geometry.y += placement.margin_top;
        else if (placement.anchor_bottom)
            geometry.y += monitor.height -
                placement.margin_bottom - thickness;
    }
    else
    {
        geometry.height = placement.height > 0
            ? placement.height
            : monitor.height -
                  placement.margin_top -
                  placement.margin_bottom;
        if (geometry.height < 1)
            geometry.height = 1;

        if (placement.anchor_top)
            geometry.y += placement.margin_top;
        else if (placement.anchor_bottom)
            geometry.y += monitor.height -
                placement.margin_bottom - geometry.height;
        else
            geometry.y +=
                (monitor.height - geometry.height) / 2;

        if (placement.anchor_left)
            geometry.x += placement.margin_left;
        else if (placement.anchor_right)
            geometry.x += monitor.width -
                placement.margin_right - thickness;
    }

    return geometry;
}

// Desktop-shell panels and GTK grabs can prevent an X11 reveal window from
// receiving crossing events at the physical output edge. Keep physical-edge
// pointer detection as a separate pure rule so the X11 reveal surface can
// poll reliably. The main-axis hit area remains aligned with the dock.
inline bool point_on_physical_reveal_edge(
    const DockPlacement &placement,
    const MonitorGeometry &monitor,
    int reveal_size,
    int pointer_x,
    int pointer_y)
{
    if (monitor.width <= 0 || monitor.height <= 0)
        return false;

    const int thickness = reveal_size > 0
        ? reveal_size
        : 1;
    const auto trigger = edge_reveal_geometry(
        placement,
        monitor,
        thickness);

    if (placement.is_horizontal())
    {
        const bool on_main_axis =
            pointer_x >= trigger.x &&
            pointer_x < trigger.x + trigger.width;
        if (!on_main_axis)
            return false;

        if (placement.anchor_top)
        {
            return pointer_y >= monitor.y &&
                   pointer_y < monitor.y + thickness;
        }

        if (placement.anchor_bottom)
        {
            return pointer_y >=
                       monitor.y + monitor.height - thickness &&
                   pointer_y < monitor.y + monitor.height;
        }

        return false;
    }

    const bool on_main_axis =
        pointer_y >= trigger.y &&
        pointer_y < trigger.y + trigger.height;
    if (!on_main_axis)
        return false;

    if (placement.anchor_left)
    {
        return pointer_x >= monitor.x &&
               pointer_x < monitor.x + thickness;
    }

    if (placement.anchor_right)
    {
        return pointer_x >=
                   monitor.x + monitor.width - thickness &&
               pointer_x < monitor.x + monitor.width;
    }

    return false;
}

struct ItemGeometry
{
    int x = 0;
    int y = 0;

    int width = 0;
    int height = 0;

    int center_x = 0;
    int center_y = 0;
};

// EWMH struts extend inward from an edge of the complete X11 root window;
// they cannot reserve an internal edge between monitor outputs. Placement
// margins are included because an existing panel can legitimately put the
// dock inward from an outer root edge while the combined reservation still
// begins at that edge.
inline bool x11_strut_reaches_root_edge(
    const DockPlacement &placement,
    int x,
    int y,
    int width,
    int height,
    int root_width,
    int root_height)
{
    if (width <= 0 || height <= 0 ||
        root_width <= 0 || root_height <= 0)
    {
        return false;
    }

    if (placement.is_vertical() &&
        placement.anchor_left)
    {
        return x - placement.margin_left == 0;
    }

    if (placement.is_vertical() &&
        placement.anchor_right)
    {
        return x + width +
                   placement.margin_right ==
               root_width;
    }

    if (placement.is_horizontal() &&
        placement.anchor_top)
    {
        return y - placement.margin_top == 0;
    }

    if (placement.is_horizontal() &&
        placement.anchor_bottom)
    {
        return y + height +
                   placement.margin_bottom ==
               root_height;
    }

    return false;
}

// A vertical X11 dock cannot slide outward when another monitor occupies
// that space: the dock would simply become visible on the adjacent output.
// Detect the corridor on either side so the native animation can collapse
// in place.
inline bool horizontal_hide_corridor_intersects_monitor(
    const DockPlacement &placement,
    int x,
    int y,
    int width,
    int height,
    const MonitorGeometry &monitor)
{
    if (!placement.is_vertical() ||
        (!placement.anchor_left &&
         !placement.anchor_right) ||
        width <= 0 || height <= 0 ||
        monitor.width <= 0 || monitor.height <= 0)
    {
        return false;
    }

    const int corridor_x = placement.anchor_right
        ? x + width
        : x - width;
    return corridor_x < monitor.x + monitor.width &&
           corridor_x + width > monitor.x &&
           y < monitor.y + monitor.height &&
           y + height > monitor.y;
}
