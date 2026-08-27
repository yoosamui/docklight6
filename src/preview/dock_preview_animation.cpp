// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_preview_animation.cpp
//
// Implementation overview:
// Implements preview presentation and opacity animation.
//
// ------------------------------------------------------------

#include "dock_preview_window.h"

#include "dock/dock_constants.h"

#include <algorithm>

namespace
{

    constexpr unsigned int GNOME_PREVIEW_REMAP_DELAY_MS = 34;
    constexpr unsigned int GNOME_PREVIEW_REVEAL_DELAY_MS = 50;

    double ease_opacity(double progress)
    {
        progress = std::clamp(
            progress,
            0.0,
            1.0);

        // Symmetric smoothstep keeps both fade directions gentle at their
        // start and finish.
        return progress * progress *
               (3.0 - 2.0 * progress);
    }
    bool uses_wayland_session()
    {
        const char *session_type =
            g_getenv("XDG_SESSION_TYPE");
        return session_type &&
               g_ascii_strcasecmp(
                   session_type,
                   "wayland") == 0;
    }
} // namespace

void DockPreviewWindow::show_preview(
    const std::vector<ApplicationWindowEntry>
        &entries,
    DockLocation location,
    const ScreenPosition &position,
    const DockPreviewSize &size)
{
    if (entries.empty())
    {
        hide_preview();
        return;
    }

    const bool remap_was_pending =
        m_gnome_preview_remap_delay.connected();

    cancel_opacity_animation();
    m_gnome_preview_remap_delay.disconnect();
    m_gnome_preview_reveal_delay.disconnect();

    m_replacing_gnome_wayland_preview =
        (get_mapped() || remap_was_pending) &&
        uses_wayland_session() &&
        m_thumbnail_provider
            .supports_gnome_live_previews();

    ++m_generation;

    if (m_replacing_gnome_wayland_preview)
    {
        // Resizing a mapped XWayland surface can make Mutter tile its previous
        // backing pixels across the new allocation before GTK repaints. That
        // is the visible phantom card. Keep the GTK surface unmapped across
        // compositor frame boundaries while retaining Shell's clone actors,
        // then rebuild and remap it with a freshly painted backing buffer.
        set_opacity(0.01);
        hide();

        const auto generation = m_generation;
        m_gnome_preview_remap_delay =
            Glib::signal_timeout().connect(
                [this,
                 entries,
                 location,
                 position,
                 size,
                 generation]()
                {
                    if (generation == m_generation)
                    {
                        present_preview(
                            entries,
                            location,
                            position,
                            size);
                    }

                    return false;
                },
                GNOME_PREVIEW_REMAP_DELAY_MS);
        return;
    }

    set_opacity(0.0);
    present_preview(entries, location, position, size);
}

void DockPreviewWindow::present_preview(
    const std::vector<ApplicationWindowEntry>
        &entries,
    DockLocation location,
    const ScreenPosition &position,
    const DockPreviewSize &size)
{
    stop_live_streams();
    m_media_title.clear();
    m_live_window_ids.clear();
    m_dynamic_refresh = false;
    rebuild(entries, size);
    set_size_request(size.width, size.height);
    set_default_size(size.width, size.height);
    m_location = location;
    m_position = position;
    m_applied_position = position;
    m_size = size;
    m_has_position = true;
    apply_position(
        location,
        position,
        size.width,
        size.height);

    // set_size_request() only changes the toplevel's minimum requisition.
    // A mapped layer-shell surface otherwise retains the previous group's
    // allocation when the new preview is smaller. Explicitly request the
    // calculated monitor-constrained allocation so the window itself, not
    // only its thumbnail children, shrinks or grows.
    resize(size.width, size.height);

    // The allocation callback applies the actual allocated geometry before
    // starting thumbnails or opacity. This prevents Shell live-preview
    // actors from being created at provisional coordinates and then replaced
    // when GTK reports the final size.
    m_presentation_pending = true;
    show_all();
    queue_resize();
}

void DockPreviewWindow::hide_preview()
{
    m_gnome_preview_remap_delay.disconnect();
    m_gnome_preview_reveal_delay.disconnect();
    m_replacing_gnome_wayland_preview = false;
    stop_live_streams();
    m_media_title.clear();
    m_live_window_ids.clear();
    m_dynamic_refresh = false;
    m_presentation_pending = false;

    for (const auto &window_id : m_window_ids)
    {
        if (m_thumbnail_cache_dirty.count(window_id) == 0)
            continue;

        const auto cached =
            m_thumbnail_cache.find(window_id);
        if (cached != m_thumbnail_cache.end())
        {
            persist_thumbnail_cache(
                window_id,
                cached->second);
        }
    }

    ++m_generation;

    if (!get_mapped())
    {
        cancel_opacity_animation();
        hide();
        clear_cards();
        set_opacity(1.0);
        return;
    }

    start_opacity_animation(true);
}

void DockPreviewWindow::hide_preview_immediately()
{
    m_gnome_preview_remap_delay.disconnect();
    m_gnome_preview_reveal_delay.disconnect();
    m_replacing_gnome_wayland_preview = false;
    stop_live_streams();
    m_media_title.clear();
    m_live_window_ids.clear();
    m_dynamic_refresh = false;
    m_presentation_pending = false;

    for (const auto &window_id : m_window_ids)
    {
        if (m_thumbnail_cache_dirty.count(window_id) == 0)
            continue;

        const auto cached =
            m_thumbnail_cache.find(window_id);
        if (cached != m_thumbnail_cache.end())
        {
            persist_thumbnail_cache(
                window_id,
                cached->second);
        }
    }

    cancel_opacity_animation();
    ++m_generation;
    hide();
    clear_cards();
    set_opacity(1.0);
}

void DockPreviewWindow::cancel_opacity_animation()
{
    if (m_opacity_timer.connected())
        m_opacity_timer.disconnect();
}

void DockPreviewWindow::complete_presentation()
{
    if (!m_presentation_pending)
        return;

    m_presentation_pending = false;

    // Live previews need the final card allocation. A dynamic-refresh request
    // can arrive while presentation is pending, so honor it now instead of
    // waiting for another playback-state notification.
    if (m_replacing_gnome_wayland_preview)
    {
        const auto generation = m_generation;
        start_live_streams(
            [this, generation](bool installed)
            {
                if (generation != m_generation ||
                    !get_visible())
                {
                    return;
                }

                queue_draw();

                if (!installed)
                {
                    set_opacity(1.0);
                    return;
                }

                // Shell installed the new clone set, but GTK and Mutter can
                // still be one paint cycle behind the allocation. Keep the
                // old clones visible until the staged GTK buffer has reached
                // Mutter, then reveal the surface.
                m_gnome_preview_reveal_delay.disconnect();
                m_gnome_preview_reveal_delay =
                    Glib::signal_timeout().connect(
                        [this, generation]()
                        {
                            if (generation == m_generation &&
                                get_visible())
                            {
                                set_opacity(1.0);
                            }

                            return false;
                        },
                        GNOME_PREVIEW_REVEAL_DELAY_MS);
            });
        m_replacing_gnome_wayland_preview = false;
        return;
    }

    if (m_thumbnail_provider.supports_gnome_live_previews() ||
        m_dynamic_refresh)
        start_live_streams();

    m_replacing_gnome_wayland_preview = false;
    start_opacity_animation(false);
}

void DockPreviewWindow::start_opacity_animation(
    bool hiding)
{
    // Opacity animation can expose an intermediate XWayland surface frame in
    // Mutter. Complete the transition immediately on GNOME Wayland while
    // retaining the existing effect on the other presentation backends.
    if (uses_wayland_session() &&
        m_thumbnail_provider.supports_gnome_live_previews())
    {
        cancel_opacity_animation();
        if (hiding)
        {
            hide();
            clear_cards();
        }
        set_opacity(1.0);
        return;
    }

    cancel_opacity_animation();

    m_opacity_animation_hiding = hiding;
    m_opacity_animation_start =
        std::clamp(
            get_opacity(),
            0.0,
            1.0);
    m_opacity_animation_target =
        hiding ? 0.0 : 1.0;
    m_opacity_animation_start_us =
        g_get_monotonic_time();

    if (std::abs(
            m_opacity_animation_target -
            m_opacity_animation_start) < 0.01)
    {
        advance_opacity_animation();
        return;
    }

    m_opacity_timer =
        Glib::signal_timeout().connect(
            sigc::mem_fun(
                *this,
                &DockPreviewWindow::
                    advance_opacity_animation),
            DockConstants::OVERLAY_ANIMATION_FRAME_MS);
}

bool DockPreviewWindow::advance_opacity_animation()
{
    const double elapsed_ms =
        static_cast<double>(
            g_get_monotonic_time() -
            m_opacity_animation_start_us) /
        1000.0;
    const double progress =
        std::clamp(
            elapsed_ms /
                std::max(
                    1,
                    DockConstants::
                        PREVIEW_FADE_DURATION_MS),
            0.0,
            1.0);
    const double eased = ease_opacity(progress);
    const double opacity =
        m_opacity_animation_start +
        (m_opacity_animation_target -
         m_opacity_animation_start) *
            eased;

    set_opacity(
        std::clamp(
            opacity,
            0.0,
            1.0));

    if (progress < 1.0)
        return true;

    cancel_opacity_animation();
    set_opacity(m_opacity_animation_target);

    if (m_opacity_animation_hiding)
    {
        hide();
        clear_cards();
        set_opacity(1.0);
    }
    return false;
}
