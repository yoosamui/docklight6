// ------------------------------------------------------------
// Docklight 6.0
// File: x11_pointer_monitor.cpp
// Purpose: Event-driven pointer observation for native X11 reveal surfaces.
// Decisions: Select raw motion on a private connection, coalesce each batch,
// and let the owner query logical pointer coordinates once. No input is
// grabbed, modified or stored. Stop removes the IO source before closing X11.
// XWayland retains its existing reveal mechanism.
// ------------------------------------------------------------

#include "x11_pointer_monitor.h"
#include "presentation_selector.h"

#include <glibmm/main.h>
#include <gdk/gdk.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

X11PointerMonitor::~X11PointerMonitor()
{
    stop();
}

bool X11PointerMonitor::start(const sigc::slot<void> &motion)
{
    stop();
    if (!is_native_x11_presentation())
        return false;

    m_display = XOpenDisplay(
        gdk_display_get_name(gdk_display_get_default()));
    if (!m_display)
        return false;

    int event = 0;
    int error = 0;
    int major = 2;
    int minor = 1;
    if (!XQueryExtension(
            m_display, "XInputExtension", &m_opcode, &event, &error) ||
        XIQueryVersion(m_display, &major, &minor) != Success)
    {
        stop();
        return false;
    }

    unsigned char bits[XIMaskLen(XI_RawMotion)]{};
    XISetMask(bits, XI_RawMotion);
    XIEventMask mask{XIAllMasterDevices, sizeof(bits), bits};
    XISelectEvents(
        m_display,
        DefaultRootWindow(m_display),
        &mask,
        1);
    XFlush(m_display);
    m_motion = motion;
    m_input = Glib::signal_io().connect(
        sigc::mem_fun(*this, &X11PointerMonitor::on_input),
        ConnectionNumber(m_display),
        Glib::IO_IN | Glib::IO_ERR | Glib::IO_HUP | Glib::IO_NVAL);
    return true;
}

void X11PointerMonitor::stop()
{
    m_input.disconnect();
    m_motion = {};
    if (m_display)
    {
        XCloseDisplay(m_display);
        m_display = nullptr;
    }
}

bool X11PointerMonitor::on_input(Glib::IOCondition condition)
{
    if (condition & (Glib::IO_ERR | Glib::IO_HUP | Glib::IO_NVAL))
    {
        stop();
        return false;
    }

    bool moved = false;
    while (m_display && XPending(m_display))
    {
        XEvent event{};
        XNextEvent(m_display, &event);
        if (event.type == GenericEvent &&
            event.xcookie.extension == m_opcode &&
            XGetEventData(m_display, &event.xcookie))
        {
            moved = moved || event.xcookie.evtype == XI_RawMotion;
            XFreeEventData(m_display, &event.xcookie);
        }
    }

    // Revealing may synchronously hide the trigger and stop this monitor.
    // Invoke a local copy and do not access the owner after the callback.
    const auto motion = m_motion;
    if (moved && motion)
        motion();
    return true;
}
