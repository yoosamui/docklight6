#pragma once

#include "dock_layout_types.h"
#include "dock_window_geometry.h"

#include <gdkmm/monitor.h>

class DockWindow;

namespace Gtk
{
class Widget;
}

class DockLayoutGeometry
{
public:
    ItemGeometry item_geometry(
        Gtk::Widget &item,
        DockWindow &dock) const;

    DockWindowGeometry dock_geometry(
        DockWindow &dock) const;

    MonitorGeometry monitor_geometry(
        const Glib::RefPtr<Gdk::Monitor>
            &monitor) const;

    // Full monitor output. Use for deterministic layer-shell placement;
    // work-area geometry is reserved for desktop-content decisions.
    MonitorGeometry output_geometry(
        const Glib::RefPtr<Gdk::Monitor>
            &monitor) const;
};
