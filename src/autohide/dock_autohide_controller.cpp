// ------------------------------------------------------------
// Docklight 6.0
//
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

#include <algorithm>
#include <cmath>

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
    cancel_animation();
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
        reveal_immediately();

    cancel_animation();
    m_has_shown_position = false;

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
        reveal_immediately();

    cancel_animation();
    m_has_shown_position = false;

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

void DockAutohideController::cancel_animation()
{
    m_animation_timer.disconnect();
}

ScreenPosition DockAutohideController::hidden_x11_position() const
{
    const int width = std::max(
        1,
        m_window.get_allocated_width());
    const int height = std::max(
        1,
        m_window.get_allocated_height());

    ScreenPosition hidden{
        m_shown_x,
        m_shown_y};

    if (m_placement.anchor_top &&
        m_placement.is_horizontal())
    {
        hidden.y -= height;
    }
    else if (m_placement.anchor_bottom &&
             m_placement.is_horizontal())
    {
        hidden.y += height;
    }
    else if (m_placement.anchor_left)
    {
        hidden.x -= width;
    }
    else if (m_placement.anchor_right)
    {
        hidden.x += width;
    }

    return hidden;
}

void DockAutohideController::animate_x11(
    bool hiding)
{
    // Layer-shell surfaces are positioned by the Wayland compositor and
    // cannot be moved one frame at a time. Xfce's X11 dock window, however,
    // has stable global coordinates and can slide cleanly through its edge.
    if (m_window.m_uses_layer_shell ||
        !m_has_placement)
    {
        if (hiding)
            m_window.hide();
        return;
    }

    int current_x = 0;
    int current_y = 0;
    m_window.get_position(current_x, current_y);

    if (!m_has_shown_position)
    {
        m_shown_x = current_x;
        m_shown_y = current_y;
        m_has_shown_position = true;
    }

    const auto hidden =
        hidden_x11_position();

    cancel_animation();

    m_animation_start_x = current_x;
    m_animation_start_y = current_y;
    m_animation_target_x =
        hiding ? hidden.x : m_shown_x;
    m_animation_target_y =
        hiding ? hidden.y : m_shown_y;
    m_animating_to_hidden = hiding;

    const double remaining = std::hypot(
        static_cast<double>(
            m_animation_target_x - current_x),
        static_cast<double>(
            m_animation_target_y - current_y));
    const double full_distance =
        std::max(
            1.0,
            std::hypot(
                static_cast<double>(
                    hidden.x - m_shown_x),
                static_cast<double>(
                    hidden.y - m_shown_y)));
    const double distance_fraction = std::clamp(
        remaining / std::max(1.0, full_distance),
        0.0,
        1.0);

    m_animation_duration_ms = std::max(
        DockConstants::AUTOHIDE_ANIMATION_FRAME_MS,
        static_cast<int>(std::lround(
            DockConstants::AUTOHIDE_ANIMATION_DURATION_MS *
            distance_fraction)));
    m_animation_start_time_us =
        g_get_monotonic_time();

    if (remaining < 0.5)
    {
        advance_x11_animation();
        return;
    }

    m_animation_timer =
        Glib::signal_timeout().connect(
            sigc::mem_fun(
                *this,
                &DockAutohideController::
                    advance_x11_animation),
            DockConstants::
                AUTOHIDE_ANIMATION_FRAME_MS);
}

bool DockAutohideController::advance_x11_animation()
{
    const double elapsed_ms =
        static_cast<double>(
            g_get_monotonic_time() -
            m_animation_start_time_us) /
        1000.0;
    const double progress = std::clamp(
        elapsed_ms /
            std::max(1, m_animation_duration_ms),
        0.0,
        1.0);

    // Hiding accelerates away from the pointer; revealing decelerates into
    // place. Both curves have a gentle edge and a decisive middle section.
    const double eased = m_animating_to_hidden
        ? progress * progress * progress
        : 1.0 - std::pow(1.0 - progress, 3.0);

    const int x = static_cast<int>(std::lround(
        m_animation_start_x +
        (m_animation_target_x - m_animation_start_x) *
            eased));
    const int y = static_cast<int>(std::lround(
        m_animation_start_y +
        (m_animation_target_y - m_animation_start_y) *
            eased));
    m_window.move(x, y);

    if (progress < 1.0)
        return true;

    m_animation_timer.disconnect();

    if (m_animating_to_hidden)
        m_window.hide();

    return false;
}

void DockAutohideController::reveal_immediately()
{
    cancel_hide();
    cancel_animation();

    if (m_hidden)
    {
        m_hidden = false;
        m_suppress_next_map_hide = true;
        m_window.show();
    }

    m_reveal_window.hide();
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
    animate_x11(true);
}

void DockAutohideController::reveal()
{
    cancel_hide();

    if (m_hidden)
    {
        m_hidden = false;

        if (!m_window.get_mapped())
        {
            m_suppress_next_map_hide = true;

            if (!m_window.m_uses_layer_shell &&
                m_has_placement &&
                m_has_shown_position)
            {
                const auto hidden =
                    hidden_x11_position();
                m_window.move(
                    hidden.x,
                    hidden.y);
            }

            m_window.show();
        }

        animate_x11(false);
    }

    m_reveal_window.hide();
}

bool DockAutohideController::can_hide() const
{
    return m_mode == DockAutohide::autohide ||
           (m_mode == DockAutohide::intellihide &&
            m_intellihide_overlap);
}
