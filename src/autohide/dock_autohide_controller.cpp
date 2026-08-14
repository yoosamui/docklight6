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
#include "windowing/window_registry.h"

#include <gdk/gdkx.h>
#include <glibmm/main.h>

#include <algorithm>
#include <cmath>

namespace
{

constexpr double X11_REVEAL_INITIAL_OPACITY = 0.18;
constexpr int X11_REVEAL_PLACEMENT_DELAY_MS = 30;

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
                if (uses_shell_reveal_trigger())
                    return false;

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
                if (uses_shell_reveal_trigger())
                    return false;

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
                if (m_pending_x11_reveal_animation)
                {
                    // The window manager can apply its ordinary-window
                    // centred position after this map event. Keep the dock
                    // transparent until DockWindowController's queued layout
                    // pass has reasserted the configured edge coordinates,
                    // then start the slide from just outside that edge.
                    m_x11_reveal_start_timer.disconnect();
                    m_x11_reveal_start_timer =
                        Glib::signal_timeout().connect(
                            [this]()
                            {
                                if (!m_pending_x11_reveal_animation)
                                    return false;

                                m_pending_x11_reveal_animation = false;
                                m_suppress_next_map_hide = false;

                                if (!m_window.get_mapped())
                                {
                                    m_window.set_opacity(1.0);
                                    return false;
                                }

                                animate_x11(false, true);
                                return false;
                            },
                            X11_REVEAL_PLACEMENT_DELAY_MS);
                    return false;
                }

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
                    request_reveal();
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

void DockAutohideController::refresh_mapped_surface()
{
    if (!m_hidden)
        return;

    // A compositor restart destroys the Shell extension's knowledge of an
    // autohidden (unmapped) ordinary Wayland window. Briefly remap it when
    // the window backend reconnects so Shell can rediscover and place the
    // dock, then restore the configured hiding policy.
    reveal_immediately();
    schedule_hide();
}

void DockAutohideController::request_reveal()
{
    if (!uses_shell_reveal_trigger())
    {
        pointer_entered();
        return;
    }

    cancel_hide();
    m_pointer_inside = false;
    reveal();
}

void DockAutohideController::set_shell_pointer_inside(
    bool inside)
{
    if (!uses_shell_reveal_trigger())
        return;

    m_pointer_inside = inside;
    if (inside)
    {
        cancel_hide();
        if (m_shell_state == ShellDockState::hiding)
            reveal();
        return;
    }

    if (m_shell_state == ShellDockState::visible)
        schedule_hide(false);
}

void DockAutohideController::finish_shell_animation(
    bool hidden)
{
    if (!uses_shell_reveal_trigger())
        return;

    if (hidden)
    {
        if (!m_hidden || m_shell_state != ShellDockState::hiding)
            return;

        m_shell_state = ShellDockState::hidden;
        set_shell_input_passthrough(true);
        return;
    }

    if (m_hidden || m_shell_state != ShellDockState::revealing)
        return;

    m_shell_state = ShellDockState::visible;
    set_shell_input_passthrough(false);
    if (!m_pointer_inside)
        schedule_hide(false);
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

void DockAutohideController::schedule_hide(
    bool refresh_pointer)
{
    // Crossing events can be lost while the transparent reveal surface is
    // replaced by the dock or while a GTK grab owns the pointer. Always
    // refresh from the real device position before pointer state can veto an
    // intellihide transition.
    if (refresh_pointer &&
        !uses_shell_reveal_trigger() &&
        m_window.get_mapped())
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
                hide_now(true);
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
    m_x11_reveal_start_timer.disconnect();
    m_animation_timer.disconnect();
}

void DockAutohideController::set_shell_input_passthrough(
    bool passthrough)
{
    auto gdk_window = m_window.get_window();
    if (gdk_window)
    {
        gdk_window->set_pass_through(passthrough);
        Cairo::RefPtr<Cairo::Region> input_region;
        if (passthrough)
            input_region = Cairo::Region::create();
        gdk_window->input_shape_combine_region(
            input_region,
            0,
            0);
    }

}

void DockAutohideController::request_shell_visibility(
    bool hidden)
{
    if (!hidden)
        set_shell_input_passthrough(false);

    m_shell_state = hidden
        ? ShellDockState::hiding
        : ShellDockState::revealing;
    if (m_window.m_window_registry)
        m_window.m_window_registry->set_dock_hidden(hidden);
}

bool DockAutohideController::can_animate_x11() const
{
    const auto display = m_window.get_display();
    return !m_window.m_uses_layer_shell &&
           display &&
           GDK_IS_X11_DISPLAY(display->gobj()) &&
           m_has_placement;
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
        hidden.y -= m_placement.margin_top > 0
            ? std::min(height, m_placement.margin_top)
            : height;
    }
    else if (m_placement.anchor_bottom &&
             m_placement.is_horizontal())
    {
        hidden.y += m_placement.margin_bottom > 0
            ? std::min(height, m_placement.margin_bottom)
            : height;
    }
    else if (m_placement.anchor_left &&
             m_placement.is_vertical())
    {
        hidden.x -= m_placement.margin_left > 0
            ? std::min(width, m_placement.margin_left)
            : width;
    }
    else if (m_placement.anchor_right &&
             m_placement.is_vertical())
    {
        hidden.x += m_placement.margin_right > 0
            ? std::min(width, m_placement.margin_right)
            : width;
    }

    return hidden;
}

void DockAutohideController::animate_x11(
    bool hiding,
    bool start_at_hidden_edge)
{
    // Layer-shell surfaces are positioned by the Wayland compositor and
    // cannot be moved one frame at a time. Xfce's X11 dock window, however,
    // has stable global coordinates and can slide cleanly through its edge.
    if (!can_animate_x11())
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

    if (!hiding && start_at_hidden_edge)
    {
        current_x = hidden.x;
        current_y = hidden.y;
        m_window.move(current_x, current_y);
        // Set opacity only after moving to the hidden edge. Raising it before
        // this move exposes one frame at the shown position during reveal.
        m_window.set_opacity(X11_REVEAL_INITIAL_OPACITY);
    }

    if (hiding)
        m_window.set_opacity(1.0);

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

    if (!m_animating_to_hidden)
    {
        m_window.set_opacity(
            X11_REVEAL_INITIAL_OPACITY +
            (1.0 - X11_REVEAL_INITIAL_OPACITY) *
                eased);
    }

    if (progress < 1.0)
        return true;

    m_animation_timer.disconnect();

    if (m_animating_to_hidden)
    {
        m_window.hide();
        m_window.set_opacity(1.0);
    }
    else
    {
        m_window.set_opacity(1.0);
    }

    return false;
}

void DockAutohideController::reveal_immediately()
{
    cancel_hide();
    cancel_animation();
    m_pending_x11_reveal_animation = false;

    if (uses_shell_reveal_trigger())
    {
        request_shell_visibility(false);
        m_hidden = false;
        m_reveal_window.hide();
        return;
    }

    if (has_shell_reveal_trigger())
    {
        // A Shell extension can disappear while its actor is transparent and
        // its input region is empty. Restore the GTK surface locally before
        // falling back to the always-visible safety state.
        set_shell_input_passthrough(false);
        if (m_window.m_window_registry)
            m_window.m_window_registry->set_dock_hidden(false);
        m_shell_state = ShellDockState::visible;
    }

    m_window.set_opacity(1.0);

    if (m_hidden)
    {
        m_hidden = false;
        m_suppress_next_map_hide = true;
        m_window.show();
    }

    m_reveal_window.hide();
}

void DockAutohideController::hide_now(
    bool refresh_pointer)
{
    if (refresh_pointer &&
        !uses_shell_reveal_trigger() &&
        m_window.get_mapped())
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

    if (uses_shell_reveal_trigger())
    {
        m_reveal_window.hide();
        // Keep the already-placed Wayland surface mapped. Unmapping it makes
        // Mutter create the next surface at the screen centre and was also
        // the source of the repeated hide/reveal cycle.
        request_shell_visibility(true);
        return;
    }

    m_reveal_window.show();
    animate_x11(true);
}

void DockAutohideController::reveal()
{
    cancel_hide();

    if (m_hidden)
    {
        m_hidden = false;

        if (uses_shell_reveal_trigger())
        {
            request_shell_visibility(false);
            m_reveal_window.hide();
            return;
        }

        if (has_shell_reveal_trigger())
        {
            set_shell_input_passthrough(false);
            if (m_window.m_window_registry)
                m_window.m_window_registry->set_dock_hidden(false);
            m_shell_state = ShellDockState::visible;
        }

        if (!m_window.get_mapped())
        {
            m_suppress_next_map_hide = true;
            const bool defer_x11_reveal =
                can_animate_x11() &&
                m_has_shown_position;

            if (defer_x11_reveal)
            {
                const auto hidden =
                    hidden_x11_position();
                m_pending_x11_reveal_animation = true;
                m_window.set_opacity(0.0);
                m_window.move(
                    hidden.x,
                    hidden.y);
            }

            m_window.show();

            if (defer_x11_reveal)
            {
                m_reveal_window.hide();
                return;
            }
        }

        animate_x11(false);
    }

    m_reveal_window.hide();
}

bool DockAutohideController::can_hide() const
{
    const bool hiding_requested =
        m_mode == DockAutohide::autohide ||
        (m_mode == DockAutohide::intellihide &&
         m_intellihide_overlap);

    // GNOME's ordinary Wayland surface has no independent edge window. If
    // the Shell integration is disconnected, hiding would leave no component
    // capable of revealing the dock again.
    return hiding_requested &&
           (!has_shell_reveal_trigger() ||
            uses_shell_reveal_trigger());
}

bool DockAutohideController::
    has_shell_reveal_trigger() const
{
    return m_window.m_window_registry &&
           m_window.m_window_registry
               ->capabilities()
               .provides_dock_reveal_trigger;
}

bool DockAutohideController::uses_shell_reveal_trigger() const
{
    return has_shell_reveal_trigger() &&
           m_window.m_window_registry->connected() &&
           m_window.m_window_registry
               ->dock_surface_geometry()
               .has_value();
}
