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

// Keep a newly mapped X11 dock fully transparent until its hidden-edge
// transform has survived one event-loop frame.  Marco can composite an
// opacity property change before it applies the preceding move request,
// otherwise exposing a dark ghost of the dock at its provisional position.
constexpr double X11_REVEAL_INITIAL_OPACITY = 0.0;
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
    DockWindow &window,
    int hide_delay_ms)
    : m_window(window),
      m_hide_delay_ms(std::max(0, hide_delay_ms))
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

void DockAutohideController::begin_initial_x11_startup()
{
    if (!m_window.surface_is_native_x11())
        return;

    m_initial_x11_startup_pending = true;
    cancel_hide();
}

void DockAutohideController::complete_initial_x11_startup()
{
    if (!m_initial_x11_startup_pending)
        return;

    m_initial_x11_startup_pending = false;
    cancel_hide();

    // The dock has remained transparent while its work area and final root
    // coordinates settled. Decide its first painted state now, instead of
    // exposing one visible frame and then running the configured autohide.
    m_pointer_inside =
        m_window.pointer_is_inside();

    if (can_hide() &&
        m_inhibit_count == 0 &&
        !pointer_inside())
    {
        hide_immediately_for_x11_startup();
        return;
    }

    reveal_immediately();
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

void DockAutohideController::set_effect(
    DockAutohideEffect effect)
{
    if (m_effect == effect)
        return;

    if (!m_initialized)
    {
        m_effect = effect;
        return;
    }

    const bool resume_hiding =
        m_mode != DockAutohide::none &&
        can_hide();
    reveal_immediately();
    m_effect = effect;

    if (resume_hiding && !pointer_inside())
        schedule_hide(false);
}

void DockAutohideController::set_hide_delay(
    int delay_ms)
{
    const int clamped_delay =
        std::max(0, delay_ms);
    if (m_hide_delay_ms == clamped_delay)
        return;

    const bool hide_pending =
        m_hide_timer.connected();
    m_hide_delay_ms = clamped_delay;

    if (hide_pending)
    {
        cancel_hide();
        schedule_hide();
    }
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
    reset_x11_visual_transform();
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
    reset_x11_visual_transform();
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

void DockAutohideController::set_backend_pointer_inside(
    bool inside)
{
    if (uses_shell_reveal_trigger())
    {
        m_shell_pointer_inside = inside;
        if (inside)
        {
            cancel_hide();
            if (m_shell_state == ShellDockState::hiding)
                reveal();
            return;
        }

        if (m_shell_state == ShellDockState::visible)
            schedule_hide(false);
        return;
    }

    if (!uses_backend_pointer_tracking())
        return;

    // KWin's workspace.cursorPos is authoritative under Wayland. The
    // XWayland root cursor can remain at its last X surface indefinitely,
    // making an absent cursor look as if it still hovered the dock.
    m_backend_pointer_inside = inside;

    if (m_hidden)
        return;

    if (inside)
        cancel_hide();
    else
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
        set_surface_input_passthrough(true);
        return;
    }

    if (m_hidden || m_shell_state != ShellDockState::revealing)
        return;

    m_shell_state = ShellDockState::visible;
    set_surface_input_passthrough(false);
    m_signal_fully_revealed.emit();
    if (!pointer_inside())
        schedule_hide(false);
}

bool DockAutohideController::is_fully_revealed() const
{
    if (m_hidden || m_pending_x11_reveal_animation)
        return false;

    if (uses_shell_reveal_trigger())
        return m_shell_state == ShellDockState::visible;

    return !m_animation_timer.connected();
}

sigc::signal<void> &
DockAutohideController::signal_fully_revealed()
{
    return m_signal_fully_revealed;
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
        !uses_backend_pointer_tracking() &&
        m_window.get_mapped())
    {
        m_pointer_inside =
            m_window.pointer_is_inside();
    }

    if (!can_hide() ||
        m_hidden ||
        m_inhibit_count > 0 ||
        pointer_inside() ||
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
            m_hide_delay_ms);
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

void DockAutohideController::reset_x11_visual_transform()
{
    m_window.set_x11_horizontal_scale(
        1.0,
        m_placement.anchor_right);
    m_window.set_x11_vertical_offset(0.0);
    m_window.set_opacity(
        m_initial_x11_startup_pending
            ? 0.0
            : 1.0);
}

void DockAutohideController::set_surface_input_passthrough(
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
        set_surface_input_passthrough(false);

    m_shell_state = hidden
        ? ShellDockState::hiding
        : ShellDockState::revealing;
    if (m_window.m_window_registry)
        m_window.m_window_registry->set_dock_hidden(hidden);
}

void DockAutohideController::animate_effect(
    bool hiding)
{
    switch (m_effect)
    {
        case DockAutohideEffect::fade:
            animate_fade(hiding);
            return;

        case DockAutohideEffect::plasma:
        case DockAutohideEffect::gnome:
        case DockAutohideEffect::slide:
            // Preserve the three baseline implementations. Plasma keeps its
            // layer-shell map/unmap transition, GNOME is dispatched to Shell
            // before reaching here, and legacy X11 keeps its slide.
            animate_x11(hiding);
            return;

        case DockAutohideEffect::scale:
        case DockAutohideEffect::slide_fade:
            g_warning(
                "Requested autohide effect is not implemented");
            return;
    }
}

void DockAutohideController::animate_fade(
    bool hiding)
{
    cancel_animation();

    // Shell-owned GNOME surfaces are dispatched before this point. GTK
    // opacity is reliable for native X11 and for Plasma's layer-shell
    // surface, so both can share the controller's existing frame timer.
    m_window.set_x11_horizontal_scale(
        1.0,
        m_placement.anchor_right);
    m_window.set_x11_vertical_offset(0.0);

    if (!hiding)
        set_surface_input_passthrough(false);

    m_animation_start_opacity =
        std::clamp(
            m_window.surface_autohide_fade_opacity(),
            0.0,
            1.0);
    m_animation_target_opacity =
        hiding ? 0.0 : 1.0;
    m_animating_to_hidden = hiding;

    const double remaining =
        std::abs(
            m_animation_target_opacity -
            m_animation_start_opacity);
    m_animation_duration_ms = std::max(
        DockConstants::AUTOHIDE_ANIMATION_FRAME_MS,
        static_cast<int>(std::lround(
            DockConstants::AUTOHIDE_ANIMATION_DURATION_MS *
            remaining)));
    m_animation_start_time_us =
        g_get_monotonic_time();

    if (remaining < 0.001)
    {
        advance_fade_animation();
        return;
    }

    m_animation_timer =
        Glib::signal_timeout().connect(
            sigc::mem_fun(
                *this,
                &DockAutohideController::
                    advance_fade_animation),
            DockConstants::
                AUTOHIDE_ANIMATION_FRAME_MS);
}

bool DockAutohideController::advance_fade_animation()
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
    const double eased =
        1.0 - std::pow(1.0 - progress, 2.0);

    m_window.set_surface_autohide_fade_opacity(
        m_animation_start_opacity +
        (m_animation_target_opacity -
         m_animation_start_opacity) *
            eased);

    if (progress < 1.0)
        return true;

    m_animation_timer.disconnect();
    m_window.set_surface_autohide_fade_opacity(
        m_animation_target_opacity);

    if (m_animating_to_hidden)
    {
        set_surface_input_passthrough(true);

        // X11 retains the existing mapped, transparent hidden surface to
        // avoid compositor map flashes. Wayland/layer-shell completes the
        // fade by entering its existing unmapped hidden state.
        m_window.finish_surface_autohide_fade(true);
    }
    else
    {
        m_window.finish_surface_autohide_fade(false);
        set_surface_input_passthrough(false);
        m_signal_fully_revealed.emit();
    }

    return false;
}

bool DockAutohideController::can_animate_x11() const
{
    const bool use_legacy_slide =
        m_effect == DockAutohideEffect::slide ||
        (m_effect == DockAutohideEffect::gnome &&
         !has_shell_reveal_trigger());

    return use_legacy_slide &&
           m_window.surface_is_native_x11() &&
           m_has_placement;
}

bool DockAutohideController::
    should_collapse_x11_horizontally() const
{
    if (!can_animate_x11() ||
        !m_placement.is_vertical() ||
        !m_has_shown_position)
    {
        return false;
    }

    auto *window = gtk_widget_get_window(
        GTK_WIDGET(m_window.gobj()));
    auto *display = window
        ? gdk_window_get_display(window)
        : nullptr;
    auto *dock_monitor = display
        ? gdk_display_get_monitor_at_window(
              display,
              window)
        : nullptr;

    if (!display || !dock_monitor)
        return false;

    const int width = std::max(
        1,
        m_window.get_allocated_width());
    const int height = std::max(
        1,
        m_window.get_allocated_height());
    const int monitor_count =
        gdk_display_get_n_monitors(display);

    for (int index = 0;
         index < monitor_count;
         ++index)
    {
        auto *monitor =
            gdk_display_get_monitor(
                display,
                index);
        if (!monitor || monitor == dock_monitor)
            continue;

        GdkRectangle rectangle{};
        gdk_monitor_get_geometry(
            monitor,
            &rectangle);
        if (horizontal_hide_corridor_intersects_monitor(
                m_placement,
                m_shown_x,
                m_shown_y,
                width,
                height,
                {
                    rectangle.x,
                    rectangle.y,
                    rectangle.width,
                    rectangle.height}))
        {
            return true;
        }
    }

    return false;
}

void DockAutohideController::animate_x11(
    bool hiding,
    bool start_at_hidden_edge)
{
    // Compositor-owned Plasma and GNOME effects cannot be moved one frame at
    // a time here. Native X11 dock windows retain their existing slide using
    // stable global coordinates.
    if (!can_animate_x11())
    {
        if (hiding)
            m_window.hide();
        else
            m_signal_fully_revealed.emit();
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

    m_animation_collapses_horizontally =
        m_placement.is_vertical() &&
        (should_collapse_x11_horizontally() ||
         m_window.x11_horizontal_scale() < 1.0);
    m_animation_clips_top =
        !m_animation_collapses_horizontally &&
        m_placement.is_horizontal() &&
        m_placement.anchor_top;
    m_animation_scale_anchor_right =
        m_placement.anchor_right;

    const auto hidden =
        x11_hidden_screen_position(
            m_placement,
            m_shown_x,
            m_shown_y,
            m_window.get_allocated_width(),
            m_window.get_allocated_height());

    if (!hiding && start_at_hidden_edge)
    {
        if (m_animation_collapses_horizontally)
        {
            current_x = m_shown_x;
            current_y = m_shown_y;
            m_window.move(current_x, current_y);
            m_window.set_x11_horizontal_scale(
                0.0,
                m_animation_scale_anchor_right);
            m_window.set_opacity(
                X11_REVEAL_INITIAL_OPACITY);
        }
        else if (m_animation_clips_top)
        {
            current_x = m_shown_x;
            current_y = m_shown_y;
            m_window.move(current_x, current_y);
            m_window.set_x11_vertical_offset(
                -m_window.get_allocated_height());
            m_window.set_opacity(
                X11_REVEAL_INITIAL_OPACITY);
        }
        else
        {
            current_x = hidden.x;
            current_y = hidden.y;
            m_window.move(current_x, current_y);
            // Keep the remapped surface transparent here. The animation
            // timer raises opacity on its first frame, after the window
            // manager has had an event-loop turn to apply this move.
            m_window.set_opacity(
                X11_REVEAL_INITIAL_OPACITY);
        }
    }

    if (hiding)
        m_window.set_opacity(1.0);

    cancel_animation();

    m_animation_start_x = current_x;
    m_animation_start_y = current_y;
    m_animation_target_x =
        hiding ? hidden.x : m_shown_x;
    m_animation_target_y =
        m_animation_collapses_horizontally ||
                m_animation_clips_top
            ? m_shown_y
            : hiding ? hidden.y : m_shown_y;
    if (m_animation_collapses_horizontally)
    {
        m_animation_target_x = m_shown_x;
        m_animation_start_scale =
            m_window.x11_horizontal_scale();
        m_animation_target_scale =
            hiding ? 0.0 : 1.0;
    }
    else
    {
        m_animation_start_scale = 1.0;
        m_animation_target_scale = 1.0;
        m_window.set_x11_horizontal_scale(
            1.0,
            m_animation_scale_anchor_right);
    }
    if (m_animation_clips_top)
    {
        m_animation_start_vertical_offset =
            m_window.x11_vertical_offset();
        m_animation_target_vertical_offset = hiding
            ? -m_window.get_allocated_height()
            : 0.0;
    }
    else
    {
        m_animation_start_vertical_offset = 0.0;
        m_animation_target_vertical_offset = 0.0;
        m_window.set_x11_vertical_offset(0.0);
    }
    m_animating_to_hidden = hiding;

    const double remaining =
        m_animation_collapses_horizontally
            ? std::abs(
                  m_animation_target_scale -
                  m_animation_start_scale) *
                  std::max(
                      1,
                      m_window.get_allocated_width())
            : m_animation_clips_top
                ? std::abs(
                      m_animation_target_vertical_offset -
                      m_animation_start_vertical_offset)
            : std::hypot(
                  static_cast<double>(
                      m_animation_target_x - current_x),
                  static_cast<double>(
                      m_animation_target_y - current_y));
    const double full_distance =
        m_animation_collapses_horizontally
            ? std::max(
                  1,
                  m_window.get_allocated_width())
            : m_animation_clips_top
                ? std::max(
                      1,
                      m_window.get_allocated_height())
            : std::max(
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

    // Apply the standard X11 timing to every edge and to the adjacent-monitor
    // collapse: accelerate while hiding, then decelerate while revealing.
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
    if (m_animation_collapses_horizontally)
    {
        m_window.set_x11_horizontal_scale(
            m_animation_start_scale +
            (m_animation_target_scale -
             m_animation_start_scale) *
                eased,
            m_animation_scale_anchor_right);
    }
    else if (m_animation_clips_top)
    {
        m_window.set_x11_vertical_offset(
            m_animation_start_vertical_offset +
            (m_animation_target_vertical_offset -
             m_animation_start_vertical_offset) *
                eased);
    }
    else
    {
        m_window.move(x, y);
    }

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
        // Keep native X11 docks mapped at their hidden transform. Marco's
        // compositor can briefly paint a shadow or stale frame whenever the
        // dock is mapped again, before our reveal animation starts. A fully
        // transparent, input-pass-through mapped surface avoids that map
        // transition while the separate edge window remains the trigger.
        m_window.set_opacity(0.0);
        set_surface_input_passthrough(true);
    }
    else
    {
        reset_x11_visual_transform();
        m_signal_fully_revealed.emit();
    }

    return false;
}

void DockAutohideController::
    hide_immediately_for_x11_startup()
{
    cancel_animation();
    m_pending_x11_reveal_animation = false;
    m_window.hide_tooltip_immediately();
    m_hidden = true;

    m_reveal_window.show();
    set_surface_input_passthrough(true);

    int current_x = 0;
    int current_y = 0;
    m_window.get_position(current_x, current_y);
    m_shown_x = current_x;
    m_shown_y = current_y;
    m_has_shown_position = true;

    m_window.set_x11_horizontal_scale(
        1.0,
        m_placement.anchor_right);
    m_window.set_x11_vertical_offset(0.0);

    if (can_animate_x11())
    {
        const bool collapse_horizontally =
            m_placement.is_vertical() &&
            should_collapse_x11_horizontally();
        const bool clip_top =
            !collapse_horizontally &&
            m_placement.is_horizontal() &&
            m_placement.anchor_top;

        if (collapse_horizontally)
        {
            m_window.set_x11_horizontal_scale(
                0.0,
                m_placement.anchor_right);
        }
        else if (clip_top)
        {
            m_window.set_x11_vertical_offset(
                -m_window.get_allocated_height());
        }
        else
        {
            const auto hidden =
                x11_hidden_screen_position(
                    m_placement,
                    m_shown_x,
                    m_shown_y,
                    m_window.get_allocated_width(),
                    m_window.get_allocated_height());
            m_window.move(hidden.x, hidden.y);
        }
    }

    // Keep the already-mapped surface transparent while the X11 window
    // manager applies the hidden transform. The edge trigger is responsible
    // for the first reveal.
    m_window.set_opacity(0.0);
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
        if (m_window.m_window_registry)
            m_window.m_window_registry->set_dock_hidden(false);
        m_shell_state = ShellDockState::visible;
    }

    reset_x11_visual_transform();
    set_surface_input_passthrough(false);

    if (m_hidden)
    {
        m_hidden = false;
        if (!m_window.get_mapped())
        {
            m_suppress_next_map_hide = true;
            m_window.show();
        }
    }

    m_reveal_window.hide();
    m_signal_fully_revealed.emit();
}

void DockAutohideController::hide_now(
    bool refresh_pointer)
{
    if (refresh_pointer &&
        !uses_shell_reveal_trigger() &&
        !uses_backend_pointer_tracking() &&
        m_window.get_mapped())
    {
        m_pointer_inside =
            m_window.pointer_is_inside();
    }

    if (!can_hide() ||
        m_hidden ||
        m_inhibit_count > 0 ||
        pointer_inside())
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

    // Native X11 keeps the dock mapped while its hide animation runs. Stop
    // routing input to it before the first frame moves or clips the surface;
    // otherwise XFWM can emit child crossing events during the transition and
    // queue a tooltip or preview after the overlays above were cleared. The
    // separate reveal window is already mapped and remains the edge trigger.
    if (m_window.surface_is_native_x11())
        set_surface_input_passthrough(true);

    animate_effect(true);
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
            set_surface_input_passthrough(false);
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
                    x11_hidden_screen_position(
                        m_placement,
                        m_shown_x,
                        m_shown_y,
                        m_window.get_allocated_width(),
                        m_window.get_allocated_height());
                m_pending_x11_reveal_animation = true;
                m_window.set_opacity(0.0);
                if (should_collapse_x11_horizontally())
                {
                    m_window.set_x11_horizontal_scale(
                        0.0,
                        m_placement.anchor_right);
                    m_window.move(
                        m_shown_x,
                        m_shown_y);
                }
                else if (m_placement.is_horizontal() &&
                         m_placement.anchor_top)
                {
                    m_window.set_x11_vertical_offset(
                        -m_window.get_allocated_height());
                    m_window.move(
                        m_shown_x,
                        m_shown_y);
                }
                else
                {
                    m_window.move(
                        hidden.x,
                        hidden.y);
                }
            }

            if (m_effect == DockAutohideEffect::fade)
                m_window.set_opacity(0.0);

            m_window.show();

            if (defer_x11_reveal)
            {
                m_reveal_window.hide();
                return;
            }
        }

        if (can_animate_x11() ||
            m_effect == DockAutohideEffect::fade)
            set_surface_input_passthrough(false);

        animate_effect(false);
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
    return !m_initial_x11_startup_pending &&
           hiding_requested &&
           (!has_shell_reveal_trigger() ||
            uses_shell_reveal_trigger());
}

bool DockAutohideController::pointer_inside() const
{
    if (uses_shell_reveal_trigger())
        return m_shell_pointer_inside;

    if (uses_backend_pointer_tracking())
        return m_backend_pointer_inside;

    return m_pointer_inside;
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
    return m_window.surface_delegates_autohide_effect(
               m_effect) &&
           has_shell_reveal_trigger() &&
           m_window.m_window_registry->connected() &&
           m_window.m_window_registry
               ->dock_surface_geometry()
               .has_value();
}

bool DockAutohideController::
    uses_backend_pointer_tracking() const
{
    return m_window.m_window_registry &&
           m_window.m_window_registry->connected() &&
           m_window.m_window_registry
               ->capabilities()
               .provides_dock_pointer_tracking;
}
