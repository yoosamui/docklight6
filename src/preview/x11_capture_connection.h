// ------------------------------------------------------------
// Docklight 6.0
// File: x11_capture_connection.h
// Purpose: Open owned capture connections with display-local error handling.
// Ownership: Caller closes the returned Display with XCloseDisplay. Each
// connection is used by one thread; error state belongs to the calling thread.
// ------------------------------------------------------------

#pragma once

#include <X11/Xlib.h>

namespace X11Capture
{
    extern thread_local bool error;
    Display *open_display();
}
