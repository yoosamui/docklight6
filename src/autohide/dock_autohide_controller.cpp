// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// dock_autohide_controller.cpp
//
// Implementation overview:
// Implements pointer-driven dock hiding and reveal timing around a
// persistent edge trigger.
//
// Important implementation decisions:
// - Placement changes are mirrored to the reveal surface.
// - Timers are cancelled and replaced as visibility intent changes.
// - Intellihide overlap participates in the same visibility policy.
//
// ------------------------------------------------------------

#include "dock_autohide_controller.h"
#include "dock/dock_constants.h"
#include "dock/dock_window.h"

#include <glibmm/main.h>

namespace
{

bool same_placement(
    const DockPlacement &left,
    const DockPlacement &right)
{
    return left.anchor_left == right.anchor_left &&
           left.anchor_right == right.anchor_right &&
           left.anchor_top == right.anchor_top &&
           left.anchor_bottom == right.anchor_bottom &&
           left.width == right.width &&
           left.height == right.height &&
           left.margin_left == right.margin_left &&
           left.margin_right == right.margin_right &&
           left.margin_top == right.margin_top &&
           left.margin_bottom == right.margin_bottom &&
           left.exclusive_zone == right.exclusive_zone &&
           left.orientation == right.orientation;
}

}

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

                if (can_hide())
                    schedule_hide();

                return false;
            });

    m_reveal_requested =
        m_reveal_window
            .signal_reveal_requested()
            .connect(
                [this]()
                {
                    // Entering the edge trigger means the pointer will be
                    // inside the revealed dock. Mapping the dock while the
                    // trigger disappears does not reliably produce a GTK
                    // enter event, so retain that physical pointer state.
                    // Otherwise intellihide maps, immediately schedules a
                    // hide, recreates the trigger under the pointer, and
                    // repeats indefinitely while a window overlaps it.
                    pointer_entered();
                });
}

void DockAutohideController::set_mode(
    DockAutohide mode)
{
    m_mode = mode;

    if (m_mode == DockAutohide::none ||
        !can_hide())
    {
        cancel_hide();
        reveal();
        return;
    }

    if (!m_hidden && m_window.get_mapped())
        schedule_hide();
}

void DockAutohideController::set_intellihide_overlap(
    bool overlap)
{
    if (m_intellihide_overlap == overlap)
        return;

    m_intellihide_overlap = overlap;

    if (m_mode != DockAutohide::intellihide)
        return;

    if (!m_intellihide_overlap)
    {
        cancel_hide();
        reveal();
        return;
    }

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
    if (m_has_placement &&
        same_placement(m_placement, placement))
    {
        return;
    }

    m_placement = placement;
    m_has_placement = true;

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
    // Crossing events can be lost while the transparent reveal surface is
    // replaced by the dock or while a GTK grab owns the pointer. Always
    // refresh from the real device position before pointer state can veto an
    // intellihide transition.
    if (m_window.get_mapped())
    {
        m_pointer_inside =
            m_window.pointer_is_inside();
    }

    if (!can_hide() ||
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
    if (m_window.get_mapped())
    {
        m_pointer_inside =
            m_window.pointer_is_inside();
    }

    if (!can_hide() ||
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

bool DockAutohideController::can_hide() const
{
    return m_mode == DockAutohide::autohide ||
           (m_mode == DockAutohide::intellihide &&
            m_intellihide_overlap);
}
