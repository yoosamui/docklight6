// ------------------------------------------------------------
// Docklight 6.0
// File: x11_capture_connection.cpp
// Purpose: Keep capture protocol errors off the process-global GDK handler.
// Decisions: Xlib's documented wire-error extension hook is per Display and
// can suppress an error before global dispatch. Install it only on our private
// connections, including core errors. XCloseDisplay owns hook-table cleanup.
// Xlibint.h supplies the extension API; isolate it from GTK/C++ provider code.
// ------------------------------------------------------------

#include "x11_capture_connection.h"
#include <X11/Xlibint.h>

namespace X11Capture
{
    thread_local bool error = false;

    namespace
    {
        Bool capture_error(Display *, XErrorEvent *, xError *)
        {
            error = true;
            return False;
        }
    }

    Display *open_display()
    {
        auto *display = XOpenDisplay(nullptr);
        if (display)
        {
            // The wire error code is one byte. Cover core and extension
            // errors without replacing any process-global error handler.
            for (int code = 0; code < 256; ++code)
                XESetWireToError(display, code, capture_error);
        }
        return display;
    }
}
