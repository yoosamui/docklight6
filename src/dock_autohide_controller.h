// ------------------------------------------------------------
// Docklight 6.0
//
// Declares pointer-driven dock autohide lifecycle and timing.
// ------------------------------------------------------------

#pragma once

#include "dock_layout_types.h"
#include "dock_reveal_window.h"

#include <gdkmm/monitor.h>
#include <sigc++/connection.h>

class DockWindow;

class DockAutohideController
{
public:
    explicit DockAutohideController(
        DockWindow &window);
    ~DockAutohideController();

    void initialize();
    void set_mode(DockAutohide mode);
    void set_monitor(
        const Glib::RefPtr<Gdk::Monitor> &monitor);
    void set_placement(
        const DockPlacement &placement);

    void inhibit();
    void uninhibit();
    void finish_drag(bool accepted);

private:
    void pointer_entered();
    void pointer_left();
    void schedule_hide();
    void cancel_hide();
    void hide_now();
    void reveal();
    bool active() const;

private:
    DockWindow &m_window;
    DockRevealWindow m_reveal_window;

    sigc::connection m_pointer_enter;
    sigc::connection m_pointer_leave;
    sigc::connection m_window_map;
    sigc::connection m_reveal_requested;
    sigc::connection m_hide_timer;

    DockAutohide m_mode = DockAutohide::none;
    int m_inhibit_count = 0;
    bool m_initialized = false;
    bool m_hidden = false;
    bool m_pointer_inside = false;
    bool m_suppress_next_map_hide = false;
};
