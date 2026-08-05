// ------------------------------------------------------------
// Docklight 6.0
//
// Implements pointer-driven autohide with a persistent edge trigger.
// ------------------------------------------------------------

#include "dock_autohide_controller.h"
#include "dock_constants.h"
#include "dock_window.h"

#include <glibmm/main.h>

DockAutohideController::DockAutohideController(
    DockWindow &window)
    : m_window(window)
{
}

DockAutohideController::~DockAutohideController()
{
    cancel_hide();
    m_pointer_enter.disconnect();
    m_pointer_leave.disconnect();
    m_window_map.disconnect();
    m_reveal_requested.disconnect();
    m_reveal_window.hide();
}

void DockAutohideController::initialize()
{
    if (m_initialized)
        return;

    m_initialized = true;

    m_window.add_events(
        Gdk::ENTER_NOTIFY_MASK |
        Gdk::LEAVE_NOTIFY_MASK);

    m_pointer_enter =
        m_window.signal_enter_notify_event().connect(
            [this](GdkEventCrossing *event)
            {
                if (!event ||
                    (event->mode == GDK_CROSSING_NORMAL &&
                     event->detail != GDK_NOTIFY_INFERIOR))
                {
                    pointer_entered();
                }

                return false;
            });

    m_pointer_leave =
        m_window.signal_leave_notify_event().connect(
            [this](GdkEventCrossing *event)
            {
                if (!event ||
                    (event->mode == GDK_CROSSING_NORMAL &&
                     event->detail != GDK_NOTIFY_INFERIOR))
                {
                    pointer_left();
                }

                return false;
            });

    m_window_map =
        m_window.signal_map_event().connect(
            [this](GdkEventAny *)
            {
                if (m_suppress_next_map_hide)
                {
                    m_suppress_next_map_hide = false;
                    return false;
                }

                if (active())
                    schedule_hide();

                return false;
            });

    m_reveal_requested =
        m_reveal_window
            .signal_reveal_requested()
            .connect(
                [this]()
                {
                    reveal();
                });
}

void DockAutohideController::set_mode(
    DockAutohide mode)
{
    m_mode = mode;

    if (!active())
    {
        cancel_hide();
        reveal();
        return;
    }

    if (!m_hidden && m_window.get_mapped())
        schedule_hide();
}

void DockAutohideController::set_monitor(
    const Glib::RefPtr<Gdk::Monitor> &monitor)
{
    const bool was_hidden = m_hidden;

    if (was_hidden)
        reveal();

    m_reveal_window.set_monitor(monitor);

    if (was_hidden)
        schedule_hide();
}

void DockAutohideController::set_placement(
    const DockPlacement &placement)
{
    const bool was_hidden = m_hidden;

    if (was_hidden)
        reveal();

    m_reveal_window.apply_placement(placement);

    if (was_hidden)
        schedule_hide();
}

void DockAutohideController::inhibit()
{
    ++m_inhibit_count;
    cancel_hide();

    if (m_hidden)
        reveal();
}

void DockAutohideController::uninhibit(
    bool pointer_inside)
{
    if (m_inhibit_count > 0)
        --m_inhibit_count;

    // Modal dialogs and popup menus own the pointer while they are open, so
    // crossing events from the dock are not authoritative when they close.
    m_pointer_inside = pointer_inside;

    if (m_pointer_inside)
    {
        cancel_hide();
        return;
    }

    if (m_inhibit_count == 0)
        schedule_hide();
}

void DockAutohideController::finish_drag(
    bool pointer_inside)
{
    if (m_inhibit_count > 0)
        --m_inhibit_count;

    // GTK's drag grab can suppress the normal crossing event, so refresh the
    // state from the physical pointer position supplied by DockWindow.
    m_pointer_inside = pointer_inside;

    if (m_pointer_inside)
    {
        cancel_hide();
        return;
    }

    if (m_inhibit_count == 0)
        schedule_hide();
}

void DockAutohideController::pointer_entered()
{
    m_pointer_inside = true;
    cancel_hide();

    if (m_hidden)
        reveal();
}

void DockAutohideController::pointer_left()
{
    m_pointer_inside = false;
    schedule_hide();
}

void DockAutohideController::schedule_hide()
{
    if (!active() ||
        m_hidden ||
        m_inhibit_count > 0 ||
        m_pointer_inside ||
        !m_window.get_mapped())
    {
        return;
    }

    cancel_hide();

    m_hide_timer =
        Glib::signal_timeout().connect(
            [this]()
            {
                hide_now();
                return false;
            },
            DockConstants::AUTOHIDE_HIDE_DELAY_MS);
}

void DockAutohideController::cancel_hide()
{
    m_hide_timer.disconnect();
}

void DockAutohideController::hide_now()
{
    if (!active() ||
        m_hidden ||
        m_inhibit_count > 0 ||
        m_pointer_inside)
    {
        return;
    }

    m_window.hide_tooltip_immediately();
    m_hidden = true;

    m_reveal_window.show();
    m_window.hide();
}

void DockAutohideController::reveal()
{
    cancel_hide();

    if (m_hidden)
    {
        m_hidden = false;
        m_suppress_next_map_hide = true;
        m_window.show();
    }

    m_reveal_window.hide();
}

bool DockAutohideController::active() const
{
    return m_mode == DockAutohide::autohide;
}
