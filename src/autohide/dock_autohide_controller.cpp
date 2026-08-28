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
constexpr double SCALE_ANCHOR_START = 0.0;
constexpr double SCALE_ANCHOR_CENTER = 0.5;
constexpr double SCALE_ANCHOR_END = 1.0;

double horizontal_scale_anchor(
    const DockPlacement &placement)
{
    return placement.anchor_right
        ? SCALE_ANCHOR_END
        : SCALE_ANCHOR_START;
}

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
                if (m_pending_surface_slide_reveal)
                {
                    // Start only after Plasma has mapped the layer surface.
                    // Advancing while the Wayland map is still pending skips
                    // the first part of the reveal and looks like a jump.
                    m_pending_surface_slide_reveal = false;
                    m_suppress_next_map_hide = false;
                    animate_surface_slide(false);
                    return false;
                }

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
    reset_local_visual_transform();
    m_has_shown_position = false;

    m_reveal_window.set_monitor(monitor);

    if (was_hidden)
        schedule_hide();
}

void DockAutohideController::set_placement(
    const DockPlacement &placement,
    const ScreenPosition &shown_position)
{
    if (m_has_placement &&
        same_placement(m_placement, placement))
    {
        return;
    }

    m_placement = placement;
    m_has_placement = true;

    const bool was_hidden = m_hidden;
    const bool preserve_hidden_wayland_surface =
        was_hidden &&
        !m_window.surface_is_native_x11();

    if (was_hidden &&
        m_window.surface_is_native_x11())
    {
        if (m_shell_animation_active &&
            uses_shell_autohide_animation())
        {
            // The compositor actor owns the hidden transform. GTK/EWMH has
            // already moved the real X11 frame to its newly calculated shown
            // position; moving that frame offscreen here would make Shell's
            // later scale/fade reveal appear at the stale edge.
            cancel_hide();
            cancel_animation();
            m_shown_x = shown_position.x;
            m_shown_y = shown_position.y;
            m_has_shown_position = true;
            m_reveal_window.apply_placement(placement);
            set_surface_input_passthrough(true);
            show_reveal_trigger();
            request_shell_visibility(true);
            return;
        }

        // apply_dock_layout() has already submitted the new shown geometry.
        // Keep the mapped native-X11 surface transparent and input-pass-
        // through while rebuilding its hidden transform around that geometry.
        // Passing through reveal_immediately() here exposes one frame whenever
        // launcher membership changes after a window opens or closes.
        apply_hidden_x11_placement(
            shown_position);
        m_reveal_window.apply_placement(placement);
        show_reveal_trigger();
        return;
    }

    if (preserve_hidden_wayland_surface)
    {
        // Launcher membership changes alter the dock's natural size. Plasma
        // keeps Slide mapped with fully clipped content; the other Wayland
        // effects can be unmapped or compositor-owned. Preserve whichever
        // hidden representation is active instead of passing through the
        // visible state merely to update the reveal strip.
        cancel_hide();
        cancel_animation();
        m_pending_x11_reveal_animation = false;
        m_has_shown_position = false;
        m_reveal_window.apply_placement(placement);

        if (uses_shell_reveal_trigger())
            return;

        if (m_effect == DockAutohideEffect::slide &&
            m_window.surface_supports_autohide_slide())
        {
            set_surface_input_passthrough(true);
            m_window.set_surface_autohide_slide_progress(
                m_placement,
                1.0);
            m_window.finish_surface_autohide_slide(true);
            show_reveal_trigger();
            return;
        }

        // A placement update can arrive while a local fade is still running.
        // Finish it invisibly, retain the normal unmapped Wayland hidden
        // state, then restore full opacity for the next explicit reveal.
        if (m_effect == DockAutohideEffect::fade)
            set_surface_input_passthrough(true);
        m_window.set_surface_autohide_fade_opacity(0.0);
        m_window.finish_surface_autohide_fade(true);
        m_window.set_surface_autohide_fade_opacity(1.0);
        show_reveal_trigger();
        return;
    }

    if (was_hidden)
        reveal_immediately();

    cancel_animation();
    reset_local_visual_transform();
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
    if (!m_shell_animation_active ||
        !uses_shell_autohide_animation())
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
    m_shell_animation_active = false;
    set_surface_input_passthrough(false);
    m_signal_fully_revealed.emit();
    if (!pointer_inside())
        schedule_hide(false);
}

bool DockAutohideController::is_fully_revealed() const
{
    if (m_hidden || m_pending_x11_reveal_animation)
        return false;

    if (m_shell_animation_active &&
        uses_shell_autohide_animation())
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
    m_pending_surface_slide_reveal = false;
}

void DockAutohideController::reset_local_visual_transform()
{
    if (m_window.surface_supports_autohide_slide())
    {
        m_window.set_surface_autohide_slide_progress(
            m_placement,
            0.0);
    }
    m_window.set_x11_horizontal_scale(
        1.0,
        horizontal_scale_anchor(m_placement));
    m_window.set_x11_vertical_scale(
        1.0,
        SCALE_ANCHOR_CENTER);
    m_window.set_x11_horizontal_offset(0.0);
    m_window.set_x11_vertical_offset(0.0);
    m_window.set_opacity(
        m_initial_x11_startup_pending
            ? 0.0
            : 1.0);
}

void DockAutohideController::apply_hidden_x11_placement(
    const ScreenPosition &shown_position)
{
    cancel_hide();
    cancel_animation();
    m_pending_x11_reveal_animation = false;
    set_surface_input_passthrough(true);

    // Opacity is the atomic guard around the asynchronous X11 resize/move.
    // Rebuild the configured visual transform while no intermediate shown
    // geometry can reach the compositor.
    m_window.set_opacity(0.0);

    // The GTK move submitted immediately before this method is asynchronous.
    // Use the layout engine's root-global target rather than reading the old
    // server position while launcher-count changes are resizing the dock.
    m_shown_x = shown_position.x;
    m_shown_y = shown_position.y;
    m_has_shown_position = true;

    m_window.set_x11_horizontal_scale(
        1.0,
        horizontal_scale_anchor(m_placement));
    m_window.set_x11_vertical_scale(
        1.0,
        SCALE_ANCHOR_CENTER);
    m_window.set_x11_horizontal_offset(0.0);
    m_window.set_x11_vertical_offset(0.0);

    if (!can_animate_x11())
        return;

    const bool collapse_horizontally =
        collapses_x11_horizontally();
    const bool collapse_vertically =
        collapses_x11_vertically();
    if (collapse_horizontally)
    {
        m_window.set_x11_horizontal_scale(
            0.0,
            x11_horizontal_collapse_anchor());
        return;
    }
    if (collapse_vertically)
    {
        m_window.set_x11_vertical_scale(
            0.0,
            SCALE_ANCHOR_CENTER);
        return;
    }

    const int width = m_placement.width > 0
        ? m_placement.width
        : std::max(1, m_window.get_allocated_width());
    const int height = m_placement.height > 0
        ? m_placement.height
        : std::max(1, m_window.get_allocated_height());

    if (m_effect == DockAutohideEffect::slide)
    {
        const auto hidden_offset =
            autohide_slide_content_offset(
                m_placement,
                width,
                height,
                1.0);
        m_window.move(m_shown_x, m_shown_y);
        m_window.set_x11_horizontal_offset(
            hidden_offset.x);
        m_window.set_x11_vertical_offset(
            hidden_offset.y);
        return;
    }

    const auto hidden =
        x11_hidden_screen_position(
            m_placement,
            m_shown_x,
            m_shown_y,
            width,
            height);
    m_window.move(hidden.x, hidden.y);
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

        case DockAutohideEffect::slide:
            if (m_window.surface_supports_autohide_slide())
            {
                animate_surface_slide(hiding);
                return;
            }

            // Native X11 owns its local Slide implementation.
            animate_x11(hiding);
            return;

        case DockAutohideEffect::slide_fade:
            g_warning(
                "Slide and fade autohide is not supported "
                "by the active surface backend; using fade");
            animate_fade(hiding);
            return;

        case DockAutohideEffect::plasma:
        case DockAutohideEffect::gnome:
            // Plasma Wayland keeps its layer-shell map/unmap transition,
            // GNOME is dispatched to Shell before reaching here, and native
            // X11 owns its local visual transition.
            animate_x11(hiding);
            return;

        case DockAutohideEffect::scale:
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
        horizontal_scale_anchor(m_placement));
    m_window.set_x11_vertical_scale(
        1.0,
        SCALE_ANCHOR_CENTER);
    m_window.set_x11_horizontal_offset(0.0);
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

void DockAutohideController::animate_surface_slide(
    bool hiding)
{
    cancel_animation();

    // The reveal strip owns edge input during the complete hide transition.
    // On reveal, restore dock input before its first visible frame.
    set_surface_input_passthrough(hiding);

    m_animation_start_progress =
        std::clamp(
            m_window.surface_autohide_slide_progress(),
            0.0,
            1.0);
    m_animation_target_progress =
        hiding ? 1.0 : 0.0;
    m_animating_to_hidden = hiding;

    const double remaining =
        std::abs(
            m_animation_target_progress -
            m_animation_start_progress);
    m_animation_duration_ms = std::max(
        DockConstants::AUTOHIDE_ANIMATION_FRAME_MS,
        static_cast<int>(std::lround(
            DockConstants::AUTOHIDE_ANIMATION_DURATION_MS *
            remaining)));
    m_animation_start_time_us =
        g_get_monotonic_time();

    if (remaining < 0.001)
    {
        advance_surface_slide_animation();
        return;
    }

    m_animation_timer =
        Glib::signal_timeout().connect(
            sigc::mem_fun(
                *this,
                &DockAutohideController::
                    advance_surface_slide_animation),
            DockConstants::
                AUTOHIDE_ANIMATION_FRAME_MS);
}

bool DockAutohideController::
    advance_surface_slide_animation()
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

    // Smoothstep has zero velocity at both endpoints, so the dock neither
    // jolts away from its edge nor stops abruptly on any orientation.
    const double eased =
        progress * progress * (3.0 - 2.0 * progress);
    const double surface_progress =
        m_animation_start_progress +
        (m_animation_target_progress -
         m_animation_start_progress) *
            eased;

    m_window.set_surface_autohide_slide_progress(
        m_placement,
        surface_progress);

    if (progress < 1.0)
        return true;

    m_animation_timer.disconnect();
    m_window.set_surface_autohide_slide_progress(
        m_placement,
        m_animation_target_progress);

    if (m_animating_to_hidden)
    {
        set_surface_input_passthrough(true);
        m_window.finish_surface_autohide_slide(true);
    }
    else
    {
        m_window.finish_surface_autohide_slide(false);
        set_surface_input_passthrough(false);
        m_signal_fully_revealed.emit();
    }

    return false;
}

bool DockAutohideController::can_animate_x11() const
{
    const bool use_native_animation =
        m_effect == DockAutohideEffect::slide ||
        m_effect == DockAutohideEffect::plasma ||
        (m_effect == DockAutohideEffect::gnome &&
         (!uses_shell_autohide_animation() ||
          !m_shell_animation_active));

    return use_native_animation &&
           m_window.surface_is_native_x11() &&
           m_has_placement;
}

bool DockAutohideController::
    uses_plasma_x11_edge_effect() const
{
    return m_effect == DockAutohideEffect::plasma &&
           m_window.surface_is_native_x11() &&
           m_has_placement;
}

bool DockAutohideController::
    collapses_x11_horizontally() const
{
    return uses_plasma_x11_edge_effect() &&
           m_placement.is_horizontal();
}

bool DockAutohideController::
    collapses_x11_vertically() const
{
    return uses_plasma_x11_edge_effect() &&
           m_placement.is_vertical();
}

double DockAutohideController::
    x11_horizontal_collapse_anchor() const
{
    return uses_plasma_x11_edge_effect()
        ? SCALE_ANCHOR_CENTER
        : horizontal_scale_anchor(m_placement);
}

void DockAutohideController::animate_x11(
    bool hiding,
    bool start_at_hidden_edge)
{
    // Native X11 owns its frame-by-frame transition. Plasma remains fixed at
    // the edge while its content collapses and fades. Slide also keeps the
    // X11 window fixed, translating its content inside the clipped surface so
    // it cannot cross an adjacent desktop panel on any edge.
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

    const bool plasma_edge_effect =
        uses_plasma_x11_edge_effect();
    m_animation_collapses_horizontally =
        collapses_x11_horizontally();
    m_animation_collapses_vertically =
        collapses_x11_vertically();
    m_animation_translates_content =
        m_effect == DockAutohideEffect::slide &&
        !m_animation_collapses_horizontally &&
        !m_animation_collapses_vertically;
    m_animation_fades = plasma_edge_effect;
    m_animation_scale_anchor =
        x11_horizontal_collapse_anchor();

    if (!m_animation_collapses_horizontally)
    {
        m_window.set_x11_horizontal_scale(
            1.0,
            horizontal_scale_anchor(m_placement));
    }
    if (!m_animation_collapses_vertically)
    {
        m_window.set_x11_vertical_scale(
            1.0,
            SCALE_ANCHOR_CENTER);
    }

    const auto hidden =
        x11_hidden_screen_position(
            m_placement,
            m_shown_x,
            m_shown_y,
            m_window.get_allocated_width(),
            m_window.get_allocated_height());
    const auto hidden_content_offset =
        autohide_slide_content_offset(
            m_placement,
            m_window.get_allocated_width(),
            m_window.get_allocated_height(),
            1.0);

    if (!hiding && start_at_hidden_edge)
    {
        if (m_animation_collapses_horizontally ||
            m_animation_collapses_vertically)
        {
            current_x = m_shown_x;
            current_y = m_shown_y;
            m_window.move(current_x, current_y);
            if (m_animation_collapses_horizontally)
            {
                m_window.set_x11_horizontal_scale(
                    0.0,
                    m_animation_scale_anchor);
            }
            else
            {
                m_window.set_x11_vertical_scale(
                    0.0,
                    m_animation_scale_anchor);
            }
            m_window.set_opacity(
                X11_REVEAL_INITIAL_OPACITY);
        }
        else if (m_animation_translates_content)
        {
            current_x = m_shown_x;
            current_y = m_shown_y;
            m_window.move(current_x, current_y);
            m_window.set_x11_horizontal_offset(
                hidden_content_offset.x);
            m_window.set_x11_vertical_offset(
                hidden_content_offset.y);
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

    if (hiding && !m_animation_fades)
        m_window.set_opacity(1.0);

    cancel_animation();

    m_animation_start_x = current_x;
    m_animation_start_y = current_y;
    m_animation_target_x =
        m_animation_collapses_horizontally ||
                m_animation_collapses_vertically ||
                m_animation_translates_content
            ? m_shown_x
            : hiding ? hidden.x : m_shown_x;
    m_animation_target_y =
        m_animation_collapses_horizontally ||
                m_animation_collapses_vertically ||
                m_animation_translates_content
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
    else if (m_animation_collapses_vertically)
    {
        m_animation_target_x = m_shown_x;
        m_animation_start_scale =
            m_window.x11_vertical_scale();
        m_animation_target_scale =
            hiding ? 0.0 : 1.0;
    }
    else
    {
        m_animation_start_scale = 1.0;
        m_animation_target_scale = 1.0;
        m_window.set_x11_horizontal_scale(
            1.0,
            m_animation_scale_anchor);
    }
    if (m_animation_translates_content)
    {
        m_animation_start_horizontal_offset =
            m_window.x11_horizontal_offset();
        m_animation_target_horizontal_offset = hiding
            ? hidden_content_offset.x
            : 0.0;
        m_animation_start_vertical_offset =
            m_window.x11_vertical_offset();
        m_animation_target_vertical_offset = hiding
            ? hidden_content_offset.y
            : 0.0;
    }
    else
    {
        m_animation_start_horizontal_offset = 0.0;
        m_animation_target_horizontal_offset = 0.0;
        m_animation_start_vertical_offset = 0.0;
        m_animation_target_vertical_offset = 0.0;
        m_window.set_x11_horizontal_offset(0.0);
        m_window.set_x11_vertical_offset(0.0);
    }
    m_animating_to_hidden = hiding;
    if (m_animation_fades)
    {
        m_animation_start_opacity =
            std::clamp(
                m_window.get_opacity(),
                0.0,
                1.0);
        m_animation_target_opacity =
            hiding ? 0.0 : 1.0;
    }
    else if (!hiding &&
             m_effect == DockAutohideEffect::slide &&
             m_animation_translates_content)
    {
        // The hidden Slide transform has already cleared the X11 pixmap. Make
        // it opaque while still fully clipped, before content starts moving
        // back from the desktop-panel edge.
        m_window.set_opacity(1.0);
    }

    const double remaining =
        m_animation_collapses_horizontally ||
                m_animation_collapses_vertically
            ? std::abs(
                  m_animation_target_scale -
                  m_animation_start_scale) *
                  std::max(
                      1,
                      m_animation_collapses_horizontally
                          ? m_window.get_allocated_width()
                          : m_window.get_allocated_height())
            : m_animation_translates_content
                ? std::hypot(
                      m_animation_target_horizontal_offset -
                          m_animation_start_horizontal_offset,
                      m_animation_target_vertical_offset -
                          m_animation_start_vertical_offset)
            : std::hypot(
                  static_cast<double>(
                      m_animation_target_x - current_x),
                  static_cast<double>(
                      m_animation_target_y - current_y));
    const double full_distance =
        m_animation_collapses_horizontally ||
                m_animation_collapses_vertically
            ? std::max(
                  1,
                  m_animation_collapses_horizontally
                      ? m_window.get_allocated_width()
                      : m_window.get_allocated_height())
            : m_animation_translates_content
                ? std::max(
                      1.0,
                      std::hypot(
                          hidden_content_offset.x,
                          hidden_content_offset.y))
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

    // Slide needs stable velocity at both endpoints; its former ease-in hide
    // bunched most movement into the final frames. Keep the established
    // asymmetric curves for Plasma and the other X11 effects so changing
    // Slide cannot alter their appearance.
    const bool smooth_slide =
        m_effect == DockAutohideEffect::slide;
    const double eased = smooth_slide
        ? progress * progress *
              (3.0 - 2.0 * progress)
        : m_animating_to_hidden
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
            m_animation_scale_anchor);
    }
    else if (m_animation_collapses_vertically)
    {
        m_window.set_x11_vertical_scale(
            m_animation_start_scale +
            (m_animation_target_scale -
             m_animation_start_scale) *
                eased,
            m_animation_scale_anchor);
    }
    else if (m_animation_translates_content)
    {
        m_window.set_x11_horizontal_offset(
            m_animation_start_horizontal_offset +
            (m_animation_target_horizontal_offset -
             m_animation_start_horizontal_offset) *
                eased);
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

    if (m_animation_fades)
    {
        m_window.set_opacity(
            m_animation_start_opacity +
            (m_animation_target_opacity -
             m_animation_start_opacity) *
                eased);
    }
    else if (!m_animating_to_hidden)
    {
        if (!(smooth_slide &&
              m_animation_translates_content))
        {
            m_window.set_opacity(
                X11_REVEAL_INITIAL_OPACITY +
                (1.0 - X11_REVEAL_INITIAL_OPACITY) *
                    eased);
        }
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
        reset_local_visual_transform();
        m_signal_fully_revealed.emit();
    }

    return false;
}

void DockAutohideController::
    hide_immediately_for_x11_startup()
{
    cancel_animation();
    m_pending_x11_reveal_animation = false;
    m_shell_animation_active = false;
    m_window.hide_tooltip_immediately();
    m_hidden = true;

    show_reveal_trigger();
    set_surface_input_passthrough(true);

    int current_x = 0;
    int current_y = 0;
    m_window.get_position(current_x, current_y);
    m_shown_x = current_x;
    m_shown_y = current_y;
    m_has_shown_position = true;

    m_window.set_x11_horizontal_scale(
        1.0,
        horizontal_scale_anchor(m_placement));
    m_window.set_x11_vertical_scale(
        1.0,
        SCALE_ANCHOR_CENTER);
    m_window.set_x11_horizontal_offset(0.0);
    m_window.set_x11_vertical_offset(0.0);

    if (can_animate_x11())
    {
        const bool collapse_horizontally =
            collapses_x11_horizontally();
        const bool collapse_vertically =
            collapses_x11_vertically();

        if (collapse_horizontally)
        {
            m_window.set_x11_horizontal_scale(
                0.0,
                x11_horizontal_collapse_anchor());
        }
        else if (collapse_vertically)
        {
            m_window.set_x11_vertical_scale(
                0.0,
                SCALE_ANCHOR_CENTER);
        }
        else if (m_effect == DockAutohideEffect::slide)
        {
            const auto hidden_offset =
                autohide_slide_content_offset(
                    m_placement,
                    m_window.get_allocated_width(),
                    m_window.get_allocated_height(),
                    1.0);
            m_window.set_x11_horizontal_offset(
                hidden_offset.x);
            m_window.set_x11_vertical_offset(
                hidden_offset.y);
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

    // Keep the already-mapped surface transparent at its hidden transform.
    // The edge trigger is responsible for the first reveal.
    m_window.set_opacity(0.0);
}

void DockAutohideController::reveal_immediately()
{
    cancel_hide();
    cancel_animation();
    m_pending_x11_reveal_animation = false;
    publish_backend_hidden_state(false);

    if (m_shell_animation_active &&
        uses_shell_autohide_animation())
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

    reset_local_visual_transform();
    m_shell_animation_active = false;
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
    publish_backend_hidden_state(true);

    if (uses_shell_autohide_animation())
    {
        m_shell_animation_active = true;
        if (uses_shell_reveal_trigger())
            m_reveal_window.hide();
        else
            show_reveal_trigger();

        // Keep the already-placed surface mapped while Shell animates its
        // compositor actor. Native X11 retains its GTK edge trigger and
        // disables input before the first transformed frame.
        if (m_window.surface_is_native_x11())
            set_surface_input_passthrough(true);
        request_shell_visibility(true);
        return;
    }

    m_shell_animation_active = false;
    show_reveal_trigger();

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
        publish_backend_hidden_state(false);

        if (m_shell_animation_active &&
            uses_shell_autohide_animation())
        {
            set_surface_input_passthrough(false);
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
            const bool defer_surface_slide_reveal =
                m_effect == DockAutohideEffect::slide &&
                m_window.surface_supports_autohide_slide();

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
                const bool collapse_horizontally =
                    collapses_x11_horizontally();
                const bool collapse_vertically =
                    collapses_x11_vertically();
                if (collapse_horizontally)
                {
                    m_window.set_x11_horizontal_scale(
                        0.0,
                        x11_horizontal_collapse_anchor());
                    m_window.move(
                        m_shown_x,
                        m_shown_y);
                }
                else if (collapse_vertically)
                {
                    m_window.set_x11_vertical_scale(
                        0.0,
                        SCALE_ANCHOR_CENTER);
                    m_window.move(
                        m_shown_x,
                        m_shown_y);
                }
                else if (m_effect ==
                         DockAutohideEffect::slide)
                {
                    const auto hidden_offset =
                        autohide_slide_content_offset(
                            m_placement,
                            m_window.get_allocated_width(),
                            m_window.get_allocated_height(),
                            1.0);
                    m_window.set_x11_horizontal_offset(
                        hidden_offset.x);
                    m_window.set_x11_vertical_offset(
                        hidden_offset.y);
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

            if (defer_surface_slide_reveal)
            {
                m_window.set_surface_autohide_slide_progress(
                    m_placement,
                    1.0);
                m_pending_surface_slide_reveal = true;
            }

            m_window.show();

            if (defer_x11_reveal ||
                defer_surface_slide_reveal)
            {
                m_reveal_window.hide();
                return;
            }
        }

        if (can_animate_x11() ||
            m_effect == DockAutohideEffect::fade ||
            (m_effect == DockAutohideEffect::slide &&
             m_window.surface_supports_autohide_slide()))
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
    uses_shell_autohide_animation() const
{
    return m_window.m_window_registry &&
           m_window.surface_delegates_autohide_effect(
               m_effect) &&
           m_window.m_window_registry->connected() &&
           m_window.m_window_registry
               ->capabilities()
               .provides_dock_autohide_animation &&
           m_window.m_window_registry
               ->dock_surface_geometry()
               .has_value();
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
    uses_backend_screen_edge_reveal() const
{
    return m_window.m_window_registry &&
           m_window.m_window_registry->connected() &&
           m_window.m_window_registry
               ->capabilities()
               .provides_dock_screen_edge_reveal;
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

void DockAutohideController::show_reveal_trigger()
{
    if (uses_backend_screen_edge_reveal())
    {
        // KWin owns the physical edge reservation on Plasma Wayland. Mapping
        // the GTK two-pixel strip underneath a stationary edge pointer emits
        // an immediate enter event and reverses the hide that just started.
        m_reveal_window.hide();
        return;
    }

    m_reveal_window.show();
}

void DockAutohideController::
    publish_backend_hidden_state(bool hidden)
{
    // KWin's Wayland script owns the compositor screen-edge trigger because
    // another Plasma panel can cover Docklight's layer-shell reveal strip.
    // Publish local autohide state only to that compositor-owned path; GNOME
    // continues to publish state as part of its delegated Shell transition.
    if (uses_backend_screen_edge_reveal() &&
        m_window.m_window_registry)
    {
        m_window.m_window_registry
            ->set_dock_hidden(hidden);
    }
}
