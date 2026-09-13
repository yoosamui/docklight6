// ------------------------------------------------------------
// Docklight 6.0
//
// File: x11_surface_stacking.h
// Purpose: Order Docklight-owned X11 surfaces through the window manager.
// This presentation helper never commands application windows. Returns false
// outside native X11 and Hyprland XWayland so other presentation paths remain
// unchanged. Hyprland queues a focus-preserving raise of the named main dock.
// ------------------------------------------------------------
#pragma once

#include <gdkmm/window.h>

bool request_x11_surface_below(
    const Glib::RefPtr<Gdk::Window> &surface,
    const Glib::RefPtr<Gdk::Window> &sibling);
