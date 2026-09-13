// ------------------------------------------------------------
// Docklight 6.0
// File: x11_pointer_monitor.h
// Purpose: Notify dock-owned surfaces of native X11 pointer movement.
// Decisions: A private XI2 connection leaves GTK's event masks untouched.
// The connection and GLib source exist only while a client needs movement
// notifications. A stationary pointer schedules no recurring wakeups.
// ------------------------------------------------------------
#pragma once

#include <glibmm/iochannel.h>
#include <sigc++/connection.h>
#include <sigc++/slot.h>

struct _XDisplay;

class X11PointerMonitor
{
public:
    X11PointerMonitor() = default;
    ~X11PointerMonitor();
    X11PointerMonitor(const X11PointerMonitor &) = delete;
    X11PointerMonitor &operator=(const X11PointerMonitor &) = delete;

    bool start(const sigc::slot<void> &motion);
    void stop();

private:
    bool on_input(Glib::IOCondition condition);
    _XDisplay *m_display = nullptr;
    int m_opcode = 0;
    sigc::connection m_input;
    sigc::slot<void> m_motion;
};
