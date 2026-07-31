// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// dock_layout_geometry.h
//
// Purpose:
// Declares the adapter that reads GTK and monitor allocations into
// plain geometry values used by the layout engine.
//
// Responsibilities:
// - Read item geometry relative to DockWindow.
// - Read dock-surface size and monitor work-area geometry.
// - Expose full output geometry for deterministic layer placement.
//
// Dependencies and ownership:
// The adapter borrows widgets, DockWindow, and Gdk::Monitor objects
// only while reading them. It owns no GTK or display resources.
//
// Design notes:
// GTK geometry extraction is isolated here so DockLayoutEngine stays
// independent from toolkit and compositor APIs.
//
// ------------------------------------------------------------

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
