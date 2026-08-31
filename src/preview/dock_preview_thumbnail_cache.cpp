// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_preview_thumbnail_cache.cpp
//
// Implementation overview:
// Implements thumbnail caching, capture recovery, and live preview streams.
//
// ------------------------------------------------------------

#include "dock_preview_window.h"
#include "dock_preview_window_internal.h"

#include <glib/gstdio.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <sstream>

namespace
{

    constexpr unsigned int X11_LIVE_REFRESH_MS = 33;
    constexpr unsigned int X11_STATIC_RETRY_MS = 80;
    constexpr unsigned int X11_STATIC_RETRY_COUNT = 8;
    constexpr unsigned int GNOME_FALLBACK_CAPTURE_DELAY_MS = 500;
    constexpr unsigned int THUMBNAIL_RECOVERY_SETTLE_MS = 500;
    constexpr unsigned int X11_CHANGE_PROBE_MS = 200;
    constexpr std::int64_t X11_LIVE_GRACE_US = 750000;
    constexpr int X11_PROBE_WIDTH = 96;
    constexpr int X11_PROBE_HEIGHT = 54;
    constexpr double X11_LIVE_OVERSAMPLE = 1.5;

    struct PersistentThumbnailPaths
    {
        std::string directory;
        std::string image;
        std::string identity;
    };

    std::string sha256(const std::string &value)
    {
        auto *checksum = g_compute_checksum_for_string(
            G_CHECKSUM_SHA256,
            value.c_str(),
            static_cast<gssize>(value.size()));
        const std::string result =
            checksum ? checksum : "";
        g_free(checksum);
        return result;
    }

    std::string process_start_time(
        std::int64_t process_id)
    {
        if (process_id <= 0)
            return {};

        const auto path =
            "/proc/" +
            std::to_string(process_id) +
            "/stat";
        gchar *contents = nullptr;
        gsize length = 0;

        if (!g_file_get_contents(
                path.c_str(),
                &contents,
                &length,
                nullptr))
        {
            return {};
        }

        const std::string stat(contents, length);
        g_free(contents);
        const auto command_end = stat.rfind(')');
        if (command_end == std::string::npos ||
            command_end + 2 >= stat.size())
        {
            return {};
        }

        std::istringstream fields(
            stat.substr(command_end + 2));
        std::string value;

        // The first token after the process name is field 3. Process start
        // time is field 22 and remains stable for the process lifetime.
        for (int field = 3; field <= 22; ++field)
        {
            if (!(fields >> value))
                return {};
        }

        return value;
    }

    std::string persistent_thumbnail_identity(
        const ApplicationWindowEntry &entry,
        bool stable_process_identity = false)
    {
        const auto start_time =
            process_start_time(entry.process_id);
        if (entry.id.empty() ||
            entry.process_id <= 0 ||
            start_time.empty())
        {
            return {};
        }

        if (stable_process_identity)
        {
            return sha256(
                "docklight-x11-thumbnail-v3\n" +
                entry.id + "\n" +
                std::to_string(entry.process_id) + "\n" +
                start_time);
        }

        return sha256(
            "docklight-xfwm-thumbnail-v3\n" +
            entry.id + "\n" +
            std::to_string(entry.process_id) + "\n" +
            start_time + "\n" +
            entry.icon_name + "\n" +
            entry.caption + "\n" +
            std::to_string(entry.frame_geometry.width) +
            "x" +
            std::to_string(entry.frame_geometry.height));
    }

    PersistentThumbnailPaths persistent_thumbnail_paths(
        const WindowId &window_id)
    {
        auto *directory_value = g_build_filename(
            g_get_user_cache_dir(),
            "docklight6",
            "window-thumbnails",
            nullptr);
        const std::string directory =
            directory_value ? directory_value : "";
        g_free(directory_value);
        const auto stem = sha256(window_id);

        return {
            directory,
            directory + G_DIR_SEPARATOR_S + stem + ".png",
            directory + G_DIR_SEPARATOR_S + stem + ".identity"};
    }

    Glib::RefPtr<Gdk::Pixbuf>
    load_persistent_thumbnail(
        const WindowId &window_id,
        const std::string &expected_identity)
    {
        if (window_id.empty() ||
            expected_identity.empty())
        {
            return {};
        }

        const auto paths =
            persistent_thumbnail_paths(window_id);
        gchar *stored_identity = nullptr;
        gsize identity_length = 0;

        if (!g_file_get_contents(
                paths.identity.c_str(),
                &stored_identity,
                &identity_length,
                nullptr))
        {
            return {};
        }

        const std::string identity(
            stored_identity,
            identity_length);
        g_free(stored_identity);

        if (identity != expected_identity)
            return {};

        try
        {
            auto thumbnail =
                Gdk::Pixbuf::create_from_file(
                paths.image);

            if (!thumbnail ||
                thumbnail->get_width() <= 0 ||
                thumbnail->get_height() <= 0 ||
                thumbnail->get_width() >
                    DockPreviewWindow::MAX_HEIGHT * 2 ||
                thumbnail->get_height() >
                    DockPreviewWindow::MAX_HEIGHT)
            {
                return {};
            }

            return thumbnail;
        }
        catch (const Glib::Error &)
        {
            return {};
        }
    }

    bool uses_xfwm_session()
    {
        const char *desktop =
            g_getenv("XDG_CURRENT_DESKTOP");
        if (!desktop)
            desktop = g_getenv("XDG_SESSION_DESKTOP");
        if (!desktop)
            return false;

        auto *normalized = g_ascii_strdown(desktop, -1);
        const bool result =
            normalized &&
            std::string(normalized).find("xfce") !=
                std::string::npos;
        g_free(normalized);
        return result;
    }

    bool uses_kde_session()
    {
        const char *desktop =
            g_getenv("XDG_CURRENT_DESKTOP");
        if (!desktop)
            desktop = g_getenv("XDG_SESSION_DESKTOP");
        if (!desktop)
            return false;

        auto *normalized = g_ascii_strdown(desktop, -1);
        const bool result =
            normalized &&
            (std::string(normalized).find("kde") !=
                 std::string::npos ||
             std::string(normalized).find("plasma") !=
                 std::string::npos);
        g_free(normalized);
        return result;
    }

    bool uses_muffin_session()
    {
        const char *desktop =
            g_getenv("XDG_CURRENT_DESKTOP");
        if (!desktop)
            desktop = g_getenv("XDG_SESSION_DESKTOP");
        if (!desktop)
            return false;

        auto *normalized = g_ascii_strdown(desktop, -1);
        const bool result =
            normalized &&
            std::string(normalized).find("cinnamon") !=
                std::string::npos;
        g_free(normalized);
        return result;
    }


    Glib::RefPtr<Gdk::Pixbuf> scaled_to_fit(
        const Glib::RefPtr<Gdk::Pixbuf> &source,
        int target_width,
        int target_height)
    {
        if (!source ||
            target_width <= 0 ||
            target_height <= 0)
        {
            return source;
        }

        const double scale = std::min({
            1.0,
            static_cast<double>(target_width) /
                source->get_width(),
            static_cast<double>(target_height) /
                source->get_height()});
        const int width = std::max(
            1,
            static_cast<int>(std::lround(
                source->get_width() * scale)));
        const int height = std::max(
            1,
            static_cast<int>(std::lround(
                source->get_height() * scale)));

        auto scaled = source;

        if (width != source->get_width() ||
            height != source->get_height())
        {
            scaled = source->scale_simple(
                width,
                height,
                Gdk::INTERP_BILINEAR);
        }

        if (!scaled || !scaled->get_has_alpha())
            return scaled;

        // Compositor captures commonly expose an alpha channel even for an
        // ordinary opaque window. If that alpha is passed to Gtk::Image, the
        // selected-card background shows through and looks as though the
        // selector was painted over the thumbnail. Flatten the displayed
        // frame onto the preview surface colour; cached capture data remains
        // untouched.
        auto opaque = Gdk::Pixbuf::create(
            Gdk::COLORSPACE_RGB,
            false,
            8,
            scaled->get_width(),
            scaled->get_height());
        if (!opaque)
            return scaled;

        constexpr unsigned char background[] = {
            28,
            28,
            32};
        const int source_channels =
            scaled->get_n_channels();
        const int destination_channels =
            opaque->get_n_channels();

        for (int y = 0; y < scaled->get_height(); ++y)
        {
            const auto *source_row =
                scaled->get_pixels() +
                y * scaled->get_rowstride();
            auto *destination_row =
                opaque->get_pixels() +
                y * opaque->get_rowstride();

            for (int x = 0; x < scaled->get_width(); ++x)
            {
                const auto *source_pixel =
                    source_row + x * source_channels;
                auto *destination_pixel =
                    destination_row +
                    x * destination_channels;
                const unsigned int alpha =
                    source_pixel[3];

                for (int channel = 0; channel < 3; ++channel)
                {
                    destination_pixel[channel] =
                        static_cast<unsigned char>(
                            (source_pixel[channel] * alpha +
                             background[channel] *
                                 (255U - alpha) +
                             127U) /
                            255U);
                }
            }
        }

        return opaque;
    }

    std::uint64_t pixbuf_signature(
        const Glib::RefPtr<Gdk::Pixbuf> &pixbuf)
    {
        if (!pixbuf)
            return 0;

        const int width = pixbuf->get_width();
        const int height = pixbuf->get_height();
        const int channels = pixbuf->get_n_channels();

        if (width <= 0 || height <= 0 || channels <= 0)
            return 0;

        const int x_step = std::max(1, width / 32);
        const int y_step = std::max(1, height / 32);
        const auto *pixels = pixbuf->get_pixels();
        const int rowstride = pixbuf->get_rowstride();
        std::uint64_t signature = 1469598103934665603ULL;

        for (int y = 0; y < height; y += y_step)
        {
            for (int x = 0; x < width; x += x_step)
            {
                const auto *pixel =
                    pixels + y * rowstride + x * channels;

                for (int channel = 0;
                     channel < std::min(3, channels);
                     ++channel)
                {
                    signature ^= pixel[channel];
                    signature *= 1099511628211ULL;
                }
            }
        }

        return signature;
    }

}
bool DockPreviewWindow::uses_mapped_thumbnail_cache() const
{
    return m_thumbnail_policy !=
           WindowThumbnailPolicy::capture_on_demand;
}

bool DockPreviewWindow::
    uses_settled_thumbnail_capture() const
{
    return m_thumbnail_policy ==
           WindowThumbnailPolicy::
               cache_mapped_windows_after_settle;
}

bool DockPreviewWindow::
    uses_redirected_thumbnail_capture() const
{
    return m_thumbnail_policy ==
           WindowThumbnailPolicy::
               redirect_and_cache_mapped_windows;
}

bool DockPreviewWindow::uses_strict_x11_capture() const
{
    return uses_xfwm_session() ||
           uses_redirected_thumbnail_capture();
}

void DockPreviewWindow::prime_thumbnail_cache(
    const std::vector<ApplicationWindowEntry>
        &entries)
{
    // GNOME's Shell capture produces a complete offscreen actor snapshot on
    // demand, including for minimized or obscured windows. Avoid eagerly
    // transferring every application's full-size PNG when the preview is
    // closed; request only the cards the user actually opens.
    if (m_thumbnail_provider
            .supports_gnome_live_previews())
        return;

    // The window backend decides whether previews need a last-known mapped
    // frame. This is independent of whether the preview surface itself uses
    // layer-shell.
    if (!m_uses_layer_shell ||
        uses_mapped_thumbnail_cache())
    {
        const bool xfwm_session =
            uses_xfwm_session();
        const bool validated_capture_session =
            xfwm_session ||
            uses_redirected_thumbnail_capture() ||
            uses_settled_thumbnail_capture();
        const bool kde_x11_session =
            !m_uses_layer_shell &&
            uses_kde_session();
        const bool mapped_cache_session =
            uses_mapped_thumbnail_cache();
        std::set<WindowId>
            application_auxiliary_window_ids;
        if (uses_redirected_thumbnail_capture())
        {
            std::vector<WindowId> redirected_windows;
            redirected_windows.reserve(entries.size());
            for (const auto &entry : entries)
            {
                if (!entry.id.empty())
                    redirected_windows.push_back(entry.id);
            }
            m_thumbnail_provider.set_x11_redirected_windows(
                redirected_windows);
        }

        const auto previously_active =
            m_thumbnail_cache_active;
        const auto previously_eligible =
            m_thumbnail_cache_eligible;
        std::set<WindowId> known_window_ids;
        std::set<WindowId> eligible_window_ids;
        std::set<WindowId> active_window_ids;
        for (const auto &entry : entries)
        {
            if (!entry.id.empty())
            {
                if (entry.application_auxiliary)
                {
                    application_auxiliary_window_ids.insert(
                        entry.id);
                }

                known_window_ids.insert(entry.id);

                const auto visible_target =
                    m_thumbnail_targets.find(
                        entry.id);
                if ((xfwm_session ||
                     kde_x11_session ||
                     mapped_cache_session) &&
                    visible_target !=
                    m_thumbnail_targets.end())
                {
                    auto &target =
                        visible_target->second;
                    const bool was_minimized =
                        target.minimized;

                    target.active =
                        entry.active;
                    target.minimized =
                        entry.minimized;
                    target.on_current_desktop =
                        entry.on_current_desktop;
                    target.application_auxiliary =
                        entry.application_auxiliary;

                    if (entry.minimized ||
                        (mapped_cache_session &&
                         !entry.on_current_desktop))
                    {
                        const auto recovered =
                            m_thumbnail_cache.find(
                                entry.id);
                        if (recovered !=
                                m_thumbnail_cache.end() &&
                            recovered->second)
                        {
                            target.image->set(
                                scaled_to_fit(
                                    recovered->second,
                                    target.target_width,
                                    target.target_height));
                            target.has_thumbnail = true;
                        }
                        else
                        {
                            target.has_thumbnail = false;
                            target.image->set_pixel_size(
                                target.fallback_size);
                            target.image->set_from_icon_name(
                                target.fallback_icon,
                                Gtk::ICON_SIZE_DIALOG);
                            show_thumbnail_fallback(
                                entry.id);
                        }
                    }
                    else if (was_minimized)
                    {
                        const auto cached =
                            m_thumbnail_cache.find(
                                entry.id);
                        if (cached !=
                                m_thumbnail_cache.end() &&
                            cached->second)
                        {
                            target.image->set(
                                scaled_to_fit(
                                    cached->second,
                                    target.target_width,
                                    target.target_height));
                            target.has_thumbnail = true;
                        }
                    }
                }

                const auto identity =
                    persistent_thumbnail_identity(
                        entry,
                        kde_x11_session ||
                            mapped_cache_session);
                const auto previous_identity =
                    m_thumbnail_cache_keys.find(
                        entry.id);
                if (previous_identity ==
                        m_thumbnail_cache_keys.end() ||
                    previous_identity->second != identity)
                {
                    m_thumbnail_cache_persisted.erase(
                        entry.id);
                }
                m_thumbnail_cache_keys[entry.id] =
                    identity;

                // Some X11 managers unmap minimized and off-workspace
                // windows. Load a complete frame saved while the window was
                // mapped, including one persisted by a previous process.
                const bool persistent_cache_session =
                    xfwm_session ||
                    kde_x11_session ||
                    mapped_cache_session;
                const bool capture_unsafe =
                    (kde_x11_session &&
                     entry.minimized) ||
                    ((xfwm_session ||
                      mapped_cache_session) &&
                     (entry.minimized ||
                      !entry.on_current_desktop));
                if (persistent_cache_session &&
                    capture_unsafe &&
                    m_thumbnail_cache.count(
                        entry.id) == 0 &&
                    !identity.empty())
                {
                    auto persisted =
                        load_persistent_thumbnail(
                            entry.id,
                            identity);
                    if (persisted)
                    {
                        m_thumbnail_cache[entry.id] =
                            std::move(persisted);
                        m_thumbnail_cache_persisted.insert(
                            entry.id);
                    }
                }

                if ((!xfwm_session &&
                     !kde_x11_session &&
                     !mapped_cache_session) ||
                    (!entry.minimized &&
                     ((!xfwm_session &&
                       !mapped_cache_session) ||
                      entry.on_current_desktop)))
                {
                    eligible_window_ids.insert(
                        entry.id);

                    if ((xfwm_session ||
                         kde_x11_session ||
                         mapped_cache_session) &&
                        entry.active)
                    {
                        active_window_ids.insert(
                            entry.id);
                    }
                }
            }
        }

        m_known_window_ids =
            std::move(known_window_ids);
        m_thumbnail_cache_eligible =
            std::move(eligible_window_ids);
        m_thumbnail_cache_active =
            std::move(active_window_ids);

        // PiP and similar auxiliaries can be continuously animated. Requiring
        // two identical post-map samples can therefore leave them in the
        // Xfwm settle quarantine indefinitely and prevent the live refresh
        // path from starting. Their live capture is already restricted to a
        // mapped, recognized application auxiliary and uses strict identity
        // validation, so remove any stale settle state immediately.
        for (const auto &window_id :
             application_auxiliary_window_ids)
        {
            const auto delay =
                m_thumbnail_cache_settle_delays.find(
                    window_id);
            if (delay !=
                m_thumbnail_cache_settle_delays.end())
            {
                delay->second.disconnect();
                m_thumbnail_cache_settle_delays.erase(
                    delay);
            }

            m_thumbnail_cache_settle_epochs.erase(
                window_id);
            m_thumbnail_candidate_signatures.erase(
                window_id);

            const auto retry =
                m_thumbnail_cache_retries.find(
                    window_id);
            if (retry != m_thumbnail_cache_retries.end())
            {
                retry->second.disconnect();
                m_thumbnail_cache_retries.erase(retry);
            }
        }

        // A newly mapped client can expose an XComposite pixmap or KWin
        // ScreenShot2 image before repainting and compositor effects finish.
        // Quarantine newly eligible windows until that transition settles,
        // then require two matching samples before replacing the cache.
        if (validated_capture_session)
        {
            for (const auto &window_id :
                 m_thumbnail_cache_eligible)
            {
                if (application_auxiliary_window_ids.count(
                        window_id) != 0 ||
                    previously_eligible.count(window_id) != 0 ||
                    m_thumbnail_cache_settle_epochs.count(
                        window_id) != 0)
                {
                    continue;
                }

                const auto settle_epoch =
                    ++m_thumbnail_cache_settle_epoch;
                m_thumbnail_cache_settle_epochs[window_id] =
                    settle_epoch;
                auto &delay =
                    m_thumbnail_cache_settle_delays[window_id];
                delay.disconnect();
                delay = Glib::signal_timeout().connect(
                    [this, window_id, settle_epoch]()
                    {
                        const auto current_epoch =
                            m_thumbnail_cache_settle_epochs.find(
                                window_id);
                        if (current_epoch ==
                                m_thumbnail_cache_settle_epochs.end() ||
                            current_epoch->second != settle_epoch)
                        {
                            return false;
                        }

                        if (m_thumbnail_cache_eligible.count(
                                window_id) == 0 ||
                            m_known_window_ids.count(
                                window_id) == 0)
                        {
                            m_thumbnail_cache_settle_epochs.erase(
                                window_id);
                            m_thumbnail_candidate_signatures.erase(
                                window_id);
                            m_thumbnail_cache_settle_delays.erase(
                                window_id);
                            return false;
                        }

                        // A request started before the workspace notification
                        // must finish and be rejected before the validated
                        // post-settle capture starts.
                        if (m_thumbnail_cache_in_flight.count(
                                window_id) != 0)
                        {
                            return true;
                        }

                        const auto target =
                            m_thumbnail_targets.find(window_id);
                        if (target != m_thumbnail_targets.end() &&
                            (target->second.capture_in_flight ||
                             target->second.probe_in_flight))
                        {
                            return true;
                        }

                        m_thumbnail_cache_settle_delays.erase(
                            window_id);
                        request_cached_thumbnail(
                            window_id,
                            X11_STATIC_RETRY_COUNT,
                            settle_epoch);
                        return false;
                    },
                    THUMBNAIL_RECOVERY_SETTLE_MS);
            }
        }

        for (auto delay =
                 m_thumbnail_cache_settle_delays.begin();
             delay != m_thumbnail_cache_settle_delays.end();)
        {
            if (m_thumbnail_cache_eligible.count(
                    delay->first) == 0)
            {
                delay->second.disconnect();
                m_thumbnail_cache_settle_epochs.erase(
                    delay->first);
                m_thumbnail_candidate_signatures.erase(
                    delay->first);
                delay = m_thumbnail_cache_settle_delays.erase(
                    delay);
            }
            else
            {
                ++delay;
            }
        }

        for (auto settling =
                 m_thumbnail_cache_settle_epochs.begin();
             settling !=
                 m_thumbnail_cache_settle_epochs.end();)
        {
            if (m_thumbnail_cache_eligible.count(
                    settling->first) == 0)
            {
                m_thumbnail_candidate_signatures.erase(
                    settling->first);
                settling = m_thumbnail_cache_settle_epochs.erase(
                    settling);
            }
            else
            {
                ++settling;
            }
        }

        for (const auto &window_id : previously_active)
        {
            if (m_thumbnail_cache_active.count(
                    window_id) == 0)
            {
                const auto cached =
                    m_thumbnail_cache.find(window_id);
                if (cached != m_thumbnail_cache.end())
                {
                    persist_thumbnail_cache(
                        window_id,
                        cached->second);
                }
            }
        }

        for (auto cached = m_thumbnail_cache.begin();
             cached != m_thumbnail_cache.end();)
        {
            if (m_known_window_ids.count(
                    cached->first) == 0)
            {
                cached = m_thumbnail_cache.erase(cached);
            }
            else
            {
                ++cached;
            }
        }

        for (auto key = m_thumbnail_cache_keys.begin();
             key != m_thumbnail_cache_keys.end();)
        {
            if (m_known_window_ids.count(key->first) == 0)
            {
                m_thumbnail_cache_persisted.erase(
                    key->first);
                m_thumbnail_cache_dirty.erase(
                    key->first);
                key = m_thumbnail_cache_keys.erase(key);
            }
            else
            {
                ++key;
            }
        }

        for (auto attempted =
                 m_cold_kwin_thumbnail_attempted.begin();
             attempted !=
                 m_cold_kwin_thumbnail_attempted.end();)
        {
            if (m_known_window_ids.count(*attempted) == 0)
            {
                attempted =
                    m_cold_kwin_thumbnail_attempted.erase(
                        attempted);
            }
            else
            {
                ++attempted;
            }
        }

        for (auto retry =
                 m_thumbnail_cache_retries.begin();
             retry != m_thumbnail_cache_retries.end();)
        {
            if (m_thumbnail_cache_eligible.count(
                    retry->first) == 0)
            {
                retry->second.disconnect();
                retry =
                    m_thumbnail_cache_retries.erase(
                        retry);
            }
            else
            {
                ++retry;
            }
        }

        // Capture a newly active mapped window once. Window metadata such as
        // a browser caption can change repeatedly while the same window stays
        // active; treating every registry update as a new activation would
        // request a full compositor screenshot each time while the preview is
        // closed. Visible previews already have their own demand-driven live
        // refresh path.
        for (const auto &window_id :
             m_thumbnail_cache_active)
        {
            if (previously_active.count(window_id) == 0)
                request_active_cache_refresh(window_id);
        }

        for (const auto &entry : entries)
        {
            if (entry.id.empty() ||
                m_thumbnail_cache_eligible.count(
                    entry.id) == 0 ||
                entry.id ==
                    m_thumbnail_recovery_active ||
                m_thumbnail_cache_settle_epochs.count(
                    entry.id) != 0 ||
                m_thumbnail_cache.count(entry.id) != 0 ||
                m_thumbnail_cache_in_flight.count(
                    entry.id) != 0)
            {
                continue;
            }

            request_cached_thumbnail(
                entry.id,
                X11_STATIC_RETRY_COUNT);
        }

        // A KWin Wayland window can still have an offscreen WindowItem when
        // normal scene painting is disabled by minimization or a different
        // virtual desktop. When DockLight starts in that state there is no
        // last-known mapped frame to reuse, so make one guarded ScreenShot2
        // request. Do not add the window to normal cache eligibility: that
        // would also enable retries and live refresh during minimize effects.
        if (uses_settled_thumbnail_capture())
        {
            for (const auto &entry : entries)
            {
                if (entry.id.empty() ||
                    (!entry.minimized &&
                     entry.on_current_desktop) ||
                    m_thumbnail_cache.count(entry.id) != 0 ||
                    m_thumbnail_cache_in_flight.count(
                        entry.id) != 0 ||
                    m_cold_kwin_thumbnail_attempted.count(
                        entry.id) != 0)
                {
                    continue;
                }

                request_cold_kwin_thumbnail(entry.id);
            }
        }

        m_thumbnail_cache_refresh.disconnect();

    }
}

void DockPreviewWindow::request_cold_kwin_thumbnail(
    const WindowId &window_id)
{
    if (window_id.empty() ||
        !uses_settled_thumbnail_capture() ||
        m_known_window_ids.count(window_id) == 0 ||
        m_thumbnail_cache_eligible.count(window_id) != 0 ||
        m_thumbnail_cache.count(window_id) != 0 ||
        m_thumbnail_cache_in_flight.count(window_id) != 0 ||
        !m_cold_kwin_thumbnail_attempted.insert(
             window_id).second)
    {
        return;
    }

    m_thumbnail_cache_in_flight.insert(window_id);

    m_thumbnail_provider.request(
        window_id,
        MAX_HEIGHT * 2,
        MAX_HEIGHT,
        [this](
            const WindowId &completed_window_id,
            const Glib::RefPtr<Gdk::Pixbuf> &thumbnail)
        {
            m_thumbnail_cache_in_flight.erase(
                completed_window_id);

            // If the window became mapped while ScreenShot2 was working,
            // discard this result. The settled mapped-window path will take
            // a fresh frame after the transition instead.
            if (!thumbnail ||
                m_known_window_ids.count(
                    completed_window_id) == 0 ||
                m_thumbnail_cache_eligible.count(
                    completed_window_id) != 0 ||
                m_thumbnail_cache.count(
                    completed_window_id) != 0)
            {
                return;
            }

            m_thumbnail_cache[completed_window_id] =
                thumbnail;
            m_thumbnail_cache_dirty.insert(
                completed_window_id);

            if (m_thumbnail_cache_persisted.count(
                    completed_window_id) == 0)
            {
                persist_thumbnail_cache(
                    completed_window_id,
                    thumbnail);
            }

            const auto target =
                m_thumbnail_targets.find(
                    completed_window_id);
            if (target != m_thumbnail_targets.end() &&
                !target->second.has_thumbnail)
            {
                target->second.image->set(
                    scaled_to_fit(
                        thumbnail,
                        target->second.target_width,
                        target->second.target_height));
                target->second.image->queue_draw();
                target->second.has_thumbnail = true;
            }
        },
        1.0,
        false,
        false);
}

void DockPreviewWindow::request_cached_thumbnail(
    const WindowId &window_id,
    unsigned int retries_remaining,
    std::uint64_t settle_epoch)
{
    const bool validate_settled_frame =
        settle_epoch != 0;
    if (window_id.empty() ||
        (!validate_settled_frame &&
         m_thumbnail_cache.count(window_id) != 0) ||
        m_thumbnail_cache_in_flight.count(
            window_id) != 0 ||
        (!validate_settled_frame &&
         m_thumbnail_cache_settle_epochs.count(
             window_id) != 0) ||
        m_thumbnail_cache_eligible.count(
            window_id) == 0)
    {
        return;
    }

    m_thumbnail_cache_in_flight.insert(window_id);

    // Remember a compositor-scaled frame while the window manager still
    // exposes its mapped pixmap. The registry can announce a new window
    // slightly before that pixmap exists, so retry cache priming just like
    // visible preview capture. Once the window is unmapped, the last valid
    // frame remains available without reading stale backing storage.
    m_thumbnail_provider.request(
        window_id,
        MAX_HEIGHT * 2,
        MAX_HEIGHT,
        [this, retries_remaining, settle_epoch](
            const WindowId &completed_window_id,
            const Glib::RefPtr<Gdk::Pixbuf> &thumbnail)
        {
            const bool validate_settled_frame =
                settle_epoch != 0;
            m_thumbnail_cache_in_flight.erase(
                completed_window_id);

            if (validate_settled_frame)
            {
                const auto current_epoch =
                    m_thumbnail_cache_settle_epochs.find(
                        completed_window_id);
                if (current_epoch ==
                        m_thumbnail_cache_settle_epochs.end() ||
                    current_epoch->second != settle_epoch)
                {
                    return;
                }
            }

            const auto retry_capture =
                [this,
                 completed_window_id,
                 retries_remaining,
                 settle_epoch]()
                {
                    auto &retry =
                        m_thumbnail_cache_retries[
                            completed_window_id];
                    retry.disconnect();
                    retry = Glib::signal_timeout().connect(
                        [this,
                         completed_window_id,
                         retries_remaining,
                         settle_epoch]()
                        {
                            request_cached_thumbnail(
                                completed_window_id,
                                retries_remaining - 1,
                                settle_epoch);
                            return false;
                        },
                        X11_STATIC_RETRY_MS);
                };

            if (thumbnail &&
                m_known_window_ids.count(
                    completed_window_id) != 0 &&
                m_thumbnail_cache_eligible.count(
                    completed_window_id) != 0 &&
                (validate_settled_frame ||
                 m_thumbnail_cache_settle_epochs.count(
                     completed_window_id) == 0))
            {
                // A capture already in flight when recovery presented the
                // window can finish while Xfwm is still painting it. Only
                // the request started after the settle delay may complete an
                // icon recovery.
                if (!validate_settled_frame &&
                    m_thumbnail_recovery_active ==
                        completed_window_id &&
                    m_thumbnail_recovery_capture_allowed.count(
                        completed_window_id) == 0)
                {
                    return;
                }

                const bool validate_repainted_window =
                    validate_settled_frame ||
                    (uses_xfwm_session() &&
                     (m_thumbnail_recovery_active ==
                          completed_window_id &&
                      m_thumbnail_recovery_capture_allowed.count(
                          completed_window_id) != 0));

                if (validate_repainted_window)
                {
                    // A newly mapped Xfwm pixmap can be readable before the
                    // client has repainted every damaged region. Treat the
                    // first frame as a candidate and only cache it after a
                    // second sampled frame agrees. Animated windows remain
                    // bounded by the normal retry count and use the newest
                    // complete readback when that count expires.
                    const auto signature =
                        pixbuf_signature(thumbnail);
                    const auto candidate =
                        m_thumbnail_candidate_signatures.find(
                            completed_window_id);

                    if (candidate ==
                            m_thumbnail_candidate_signatures.end() ||
                        candidate->second != signature)
                    {
                        m_thumbnail_candidate_signatures[
                            completed_window_id] = signature;

                        if (retries_remaining > 0)
                        {
                            retry_capture();
                            return;
                        }
                    }

                    m_thumbnail_candidate_signatures.erase(
                        completed_window_id);
                    m_thumbnail_recovery_capture_allowed.erase(
                        completed_window_id);
                    if (validate_settled_frame)
                    {
                        m_thumbnail_cache_settle_epochs.erase(
                            completed_window_id);
                    }
                }

                m_thumbnail_cache[completed_window_id] =
                    thumbnail;
                m_thumbnail_cache_dirty.insert(
                    completed_window_id);
                m_thumbnail_recovery_requested.erase(
                    completed_window_id);

                if (m_thumbnail_cache_persisted.count(
                        completed_window_id) == 0)
                {
                    persist_thumbnail_cache(
                        completed_window_id,
                        thumbnail);
                }

                const auto target =
                    m_thumbnail_targets.find(
                        completed_window_id);
                if (target != m_thumbnail_targets.end() &&
                    (!target->second.has_thumbnail ||
                     validate_settled_frame))
                {
                    target->second.image->set(
                        scaled_to_fit(
                            thumbnail,
                            target->second.target_width,
                            target->second.target_height));
                    target->second.image->queue_draw();
                    target->second.has_thumbnail = true;
                }

                if (m_thumbnail_recovery_active ==
                    completed_window_id)
                {
                    m_thumbnail_recovery_active.clear();
                    start_next_thumbnail_recovery();
                }

                const auto retry =
                    m_thumbnail_cache_retries.find(
                        completed_window_id);
                if (retry !=
                    m_thumbnail_cache_retries.end())
                {
                    retry->second.disconnect();
                    m_thumbnail_cache_retries.erase(retry);
                }
                return;
            }

            if (retries_remaining == 0 ||
                m_thumbnail_cache_eligible.count(
                    completed_window_id) == 0)
            {
                m_thumbnail_candidate_signatures.erase(
                    completed_window_id);
                m_thumbnail_recovery_capture_allowed.erase(
                    completed_window_id);
                if (validate_settled_frame)
                {
                    m_thumbnail_cache_settle_epochs.erase(
                        completed_window_id);
                }

                if (m_thumbnail_recovery_active ==
                    completed_window_id)
                {
                    m_thumbnail_recovery_requested.erase(
                        completed_window_id);
                    m_thumbnail_recovery_active.clear();
                    start_next_thumbnail_recovery();
                }

                return;
            }

            retry_capture();
        },
        1.0,
        uses_xfwm_session() &&
            (validate_settled_frame ||
             m_thumbnail_recovery_capture_allowed.count(
                 window_id) != 0),
        uses_strict_x11_capture());
}

void DockPreviewWindow::request_active_cache_refresh(
    const WindowId &window_id)
{
    if (window_id.empty() ||
        m_thumbnail_cache_active.count(window_id) == 0 ||
        m_thumbnail_cache_in_flight.count(
            window_id) != 0 ||
        m_thumbnail_cache_settle_epochs.count(
            window_id) != 0 ||
        m_thumbnail_recovery_requested.count(
            window_id) != 0 ||
        (m_dynamic_refresh &&
         visible_for(window_id)))
    {
        return;
    }

    m_thumbnail_cache_in_flight.insert(window_id);

    // Refresh only the active, mapped window. The resulting valid frame is
    // the last-known image shown after minimization. Xfwm alone needs the
    // guarded native fallback; other X11 compositors use XComposite.
    m_thumbnail_provider.request(
        window_id,
        MAX_HEIGHT * 2,
        MAX_HEIGHT,
        [this](
            const WindowId &completed_window_id,
            const Glib::RefPtr<Gdk::Pixbuf> &thumbnail)
        {
            m_thumbnail_cache_in_flight.erase(
                completed_window_id);

            if (!thumbnail ||
                m_thumbnail_cache_settle_epochs.count(
                    completed_window_id) != 0 ||
                m_thumbnail_cache_active.count(
                    completed_window_id) == 0 ||
                m_known_window_ids.count(
                    completed_window_id) == 0)
            {
                return;
            }

            m_thumbnail_cache[completed_window_id] =
                thumbnail;
            m_thumbnail_cache_dirty.insert(
                completed_window_id);

            if (m_thumbnail_cache_persisted.count(
                    completed_window_id) == 0)
            {
                persist_thumbnail_cache(
                    completed_window_id,
                    thumbnail);
            }

            const auto target =
                m_thumbnail_targets.find(
                    completed_window_id);
            if (target != m_thumbnail_targets.end())
            {
                target->second.image->set(
                    scaled_to_fit(
                        thumbnail,
                        target->second.target_width,
                        target->second.target_height));
                target->second.image->queue_draw();
                target->second.has_thumbnail = true;
            }
        },
        1.0,
        uses_xfwm_session(),
        uses_strict_x11_capture());
}

void DockPreviewWindow::persist_thumbnail_cache(
    const WindowId &window_id,
    const Glib::RefPtr<Gdk::Pixbuf> &thumbnail)
{
    if (m_uses_layer_shell ||
        (!uses_xfwm_session() &&
         !uses_kde_session() &&
         !uses_mapped_thumbnail_cache()) ||
        window_id.empty() ||
        !thumbnail)
    {
        return;
    }

    const auto identity =
        m_thumbnail_cache_keys.find(window_id);
    if (identity == m_thumbnail_cache_keys.end() ||
        identity->second.empty())
    {
        return;
    }

    const auto paths =
        persistent_thumbnail_paths(window_id);
    if (paths.directory.empty() ||
        g_mkdir_with_parents(
            paths.directory.c_str(),
            0700) != 0)
    {
        return;
    }

    const auto image_temporary =
        paths.image + ".tmp";
    const auto identity_temporary =
        paths.identity + ".tmp";

    try
    {
        thumbnail->save(
            image_temporary,
            "png");
    }
    catch (const Glib::Error &error)
    {
        g_warning(
            "Cannot persist window thumbnail: %s",
            error.what().c_str());
        g_unlink(image_temporary.c_str());
        return;
    }

    if (g_rename(
            image_temporary.c_str(),
            paths.image.c_str()) != 0)
    {
        g_unlink(image_temporary.c_str());
        return;
    }

    GError *error = nullptr;
    if (!g_file_set_contents(
            identity_temporary.c_str(),
            identity->second.c_str(),
            static_cast<gssize>(
                identity->second.size()),
            &error) ||
        g_rename(
            identity_temporary.c_str(),
            paths.identity.c_str()) != 0)
    {
        g_clear_error(&error);
        g_unlink(identity_temporary.c_str());
        return;
    }

    g_clear_error(&error);
    m_thumbnail_cache_persisted.insert(window_id);
    m_thumbnail_cache_dirty.erase(window_id);
}

void DockPreviewWindow::request_thumbnail(
    const WindowId &window_id,
    unsigned int generation)
{
    const auto target =
        m_thumbnail_targets.find(window_id);

    if (target == m_thumbnail_targets.end() ||
        target->second.capture_in_flight)
    {
        return;
    }

    if (m_thumbnail_cache_settle_epochs.count(window_id) != 0)
    {
        if (!target->second.has_thumbnail)
        {
            target->second.image->set_pixel_size(
                target->second.fallback_size);
            target->second.image->set_from_icon_name(
                target->second.fallback_icon,
                Gtk::ICON_SIZE_DIALOG);
        }
        return;
    }

    // Minimized captures can contain unmapped storage or compositor effects.
    // Keep the last frame captured while mapped; only use the icon when no
    // cache exists.
    const bool kde_x11_session =
        !m_uses_layer_shell &&
        uses_kde_session();
    if ((kde_x11_session &&
         target->second.minimized) ||
        ((uses_xfwm_session() ||
          uses_mapped_thumbnail_cache()) &&
         (target->second.minimized ||
          !target->second.on_current_desktop)))
    {
        if (!target->second.has_thumbnail)
            show_thumbnail_fallback(window_id);
        return;
    }

    target->second.capture_in_flight = true;

    m_thumbnail_provider.request(
        window_id,
        target->second.target_width,
        target->second.target_height,
        [this,
         generation,
         window_id](
            const WindowId &,
            const Glib::RefPtr<Gdk::Pixbuf>
                &thumbnail)
        {
            if (generation != m_generation)
                return;

            const auto completed =
                m_thumbnail_targets.find(
                    window_id);

            if (completed ==
                m_thumbnail_targets.end())
            {
                return;
            }

            auto &target = completed->second;
            target.capture_in_flight = false;

            if (thumbnail &&
                m_thumbnail_cache_settle_epochs.count(
                    window_id) == 0 &&
                ((!uses_mapped_thumbnail_cache() &&
                  (m_uses_layer_shell ||
                   (!uses_xfwm_session() &&
                    !uses_kde_session()))) ||
                 m_thumbnail_cache_eligible.count(
                     window_id) != 0))
            {
                m_thumbnail_cache[window_id] =
                    thumbnail;
                m_thumbnail_cache_dirty.insert(
                    window_id);
                target.image->set(
                    scaled_to_fit(
                        thumbnail,
                        target.target_width,
                        target.target_height));
                target.image->queue_draw();
                target.has_thumbnail = true;
                m_thumbnail_recovery_requested.erase(
                    window_id);

                if (m_thumbnail_recovery_active ==
                    window_id)
                {
                    m_thumbnail_recovery_active.clear();
                    start_next_thumbnail_recovery();
                }
            }
            else if (!target.has_thumbnail)
            {
                if (target.initial_capture_failures <
                        X11_STATIC_RETRY_COUNT)
                {
                    ++target.initial_capture_failures;
                    Glib::signal_timeout().connect(
                        [this,
                         window_id,
                         generation]()
                        {
                            if (generation == m_generation)
                            {
                                request_thumbnail(
                                    window_id,
                                    generation);
                            }

                            return false;
                        },
                        X11_STATIC_RETRY_MS);
                    return;
                }

                show_thumbnail_fallback(
                    window_id);
            }
        },
        2.0,
        false,
        uses_strict_x11_capture());
}

void DockPreviewWindow::show_thumbnail_fallback(
    const WindowId &window_id)
{
    const auto found =
        m_thumbnail_targets.find(window_id);

    if (found == m_thumbnail_targets.end() ||
        found->second.has_thumbnail)
    {
        return;
    }

    auto &target = found->second;
    target.image->set_pixel_size(
        target.fallback_size);
    target.image->set_from_icon_name(
        target.fallback_icon,
        Gtk::ICON_SIZE_DIALOG);

    // Xfwm cannot provide a reliable pixmap for an unmapped window. Only an
    // actual icon fallback requests this visible recovery: the controller
    // visits the window's workspace and presents it, after which cache
    // priming captures the newly mapped pixmap and replaces this icon.
    if (uses_xfwm_session() &&
        m_thumbnail_recovery_requested.insert(
            window_id).second)
    {
        m_thumbnail_recovery_queue.push_back(
            window_id);
        start_next_thumbnail_recovery();
    }
}

void DockPreviewWindow::start_next_thumbnail_recovery()
{
    if (!m_thumbnail_recovery_active.empty())
        return;

    while (!m_thumbnail_recovery_queue.empty())
    {
        const auto window_id =
            m_thumbnail_recovery_queue.front();
        m_thumbnail_recovery_queue.pop_front();

        const auto target =
            m_thumbnail_targets.find(window_id);
        if (target == m_thumbnail_targets.end() ||
            target->second.has_thumbnail ||
            m_thumbnail_recovery_requested.count(
                window_id) == 0)
        {
            continue;
        }

        m_thumbnail_recovery_active = window_id;
        m_thumbnail_candidate_signatures.erase(
            window_id);
        m_reload_thumbnail.emit(window_id);

        // Workspace activation and unminimize are asynchronous in Xfwm.
        // Wait beyond the animation/first-paint interval, then start a fresh
        // capture. If the registry has not observed the new workspace yet,
        // keep waiting instead of caching a partial or stale pixmap.
        m_thumbnail_recovery_delay.disconnect();
        m_thumbnail_recovery_delay =
            Glib::signal_timeout().connect(
                [this, window_id]()
                {
                    if (m_thumbnail_recovery_active !=
                        window_id)
                    {
                        return false;
                    }

                    if (m_thumbnail_cache_eligible.count(
                            window_id) == 0 ||
                        m_thumbnail_cache_in_flight.count(
                            window_id) != 0)
                    {
                        return true;
                    }

                    m_thumbnail_recovery_capture_allowed.insert(
                        window_id);
                    request_cached_thumbnail(
                        window_id,
                        X11_STATIC_RETRY_COUNT);
                    return false;
                },
                THUMBNAIL_RECOVERY_SETTLE_MS);
        return;
    }
}

void DockPreviewWindow::request_x11_change_probe(
    const WindowId &window_id,
    unsigned int generation)
{
    const auto found =
        m_thumbnail_targets.find(window_id);

    if (found == m_thumbnail_targets.end() ||
        found->second.capture_in_flight ||
        found->second.probe_in_flight ||
        m_thumbnail_cache_settle_epochs.count(
            window_id) != 0 ||
        found->second.minimized ||
        !found->second.on_current_desktop)
    {
        return;
    }

    found->second.probe_in_flight = true;

    m_thumbnail_provider.request(
        window_id,
        X11_PROBE_WIDTH,
        X11_PROBE_HEIGHT,
        [this, generation](
            const WindowId &completed_window_id,
            const Glib::RefPtr<Gdk::Pixbuf> &frame)
        {
            if (generation != m_generation)
                return;

            const auto completed =
                m_thumbnail_targets.find(
                    completed_window_id);

            if (completed == m_thumbnail_targets.end())
                return;

            auto &target = completed->second;
            target.probe_in_flight = false;

            if (!frame ||
                m_thumbnail_cache_settle_epochs.count(
                    completed_window_id) != 0 ||
                (!m_uses_layer_shell &&
                 (uses_kde_session() ||
                  uses_mapped_thumbnail_cache()) &&
                 m_thumbnail_cache_eligible.count(
                     completed_window_id) == 0))
                return;

            const auto signature =
                pixbuf_signature(frame);

            if (target.has_probe_signature &&
                signature != target.probe_signature)
            {
                target.live_until_us =
                    g_get_monotonic_time() +
                    X11_LIVE_GRACE_US;
            }

            target.has_probe_signature = true;
            target.probe_signature = signature;
        },
        1.0,
        uses_xfwm_session(),
        uses_strict_x11_capture());
}

void DockPreviewWindow::request_live_x11_thumbnail(
    const WindowId &window_id,
    unsigned int generation,
    bool allow_xfwm_group_fallback)
{
    const auto found =
        m_thumbnail_targets.find(window_id);

    if (found == m_thumbnail_targets.end() ||
        found->second.capture_in_flight ||
        m_thumbnail_cache_settle_epochs.count(
            window_id) != 0 ||
        found->second.minimized ||
        !found->second.on_current_desktop)
    {
        return;
    }

    // This XFCE session exposes the Composite extension without redirected
    // per-window pixmaps. Permit the guarded window-drawable fallback only
    // for a media window that is active, classified as an application
    // auxiliary (for example Picture-in-Picture), or exactly selected through
    // the current MPRIS title. Ordinary cards and cache priming remain
    // composite-only, preserving their identity guarantees.
    const bool allow_xfwm_media_fallback =
        uses_xfwm_session() &&
        (found->second.active ||
         found->second.application_auxiliary ||
         allow_xfwm_group_fallback);

    found->second.capture_in_flight = true;

    m_thumbnail_provider.request(
        window_id,
        found->second.target_width,
        found->second.target_height,
        [this, generation](
            const WindowId &completed_window_id,
            const Glib::RefPtr<Gdk::Pixbuf> &frame)
        {
            if (generation != m_generation)
                return;

            const auto completed =
                m_thumbnail_targets.find(
                    completed_window_id);

            if (completed == m_thumbnail_targets.end())
                return;

            auto &thumbnail = completed->second;
            thumbnail.capture_in_flight = false;

            if (!frame ||
                m_thumbnail_cache_settle_epochs.count(
                    completed_window_id) != 0 ||
                (!m_uses_layer_shell &&
                 (uses_kde_session() ||
                  uses_mapped_thumbnail_cache()) &&
                 m_thumbnail_cache_eligible.count(
                     completed_window_id) == 0))
                return;

            const auto signature =
                pixbuf_signature(frame);

            if (thumbnail.has_live_signature &&
                signature == thumbnail.live_signature)
            {
                return;
            }

            thumbnail.has_live_signature = true;
            thumbnail.live_signature = signature;
            thumbnail.live_until_us =
                g_get_monotonic_time() +
                X11_LIVE_GRACE_US;
            thumbnail.image->set(
                scaled_to_fit(
                    frame,
                    thumbnail.target_width,
                    thumbnail.target_height));
            thumbnail.image->queue_draw();
            m_thumbnail_cache[completed_window_id] = frame;
            m_thumbnail_cache_dirty.insert(
                completed_window_id);
            thumbnail.has_thumbnail = true;
        },
        X11_LIVE_OVERSAMPLE,
        allow_xfwm_media_fallback,
        uses_strict_x11_capture());
}

void DockPreviewWindow::start_live_streams(
    DockWindowThumbnailProvider::LivePreviewsCallback
        callback)
{
    if (m_presentation_pending)
    {
        if (callback)
            callback(false);
        return;
    }

    const auto generation = m_generation;
    std::set<WindowId> desired_windows;
    const bool uses_gnome_live_previews =
        m_thumbnail_provider
            .supports_gnome_live_previews();

    if (uses_gnome_live_previews)
    {
        // Color is independent of the visible-window set. Publish it even
        // when the existing compositor previews can otherwise be reused.
        m_thumbnail_provider.set_gnome_preview_color(
            m_preview_color.get_red(),
            m_preview_color.get_green(),
            m_preview_color.get_blue(),
            m_preview_color.get_alpha());
    }

    for (const auto &entry : m_thumbnail_targets)
    {
        // Shell.WindowPreviewLayout can paint minimized windows just as the
        // overview does. Keep every GNOME card in the requested set; otherwise
        // a fully minimized group produces an empty set and returns below
        // before Shell is asked to create any previews.
        if (uses_gnome_live_previews)
        {
            desired_windows.insert(entry.first);
            continue;
        }

        // Refresh only windows the compositor is expected to keep painting.
        // Hidden windows are allowed to retain their last frame.
        if (!entry.second.minimized &&
            entry.second.on_current_desktop)
            desired_windows.insert(entry.first);
    }

    if (desired_windows == m_live_window_ids)
    {
        if (callback)
            callback(true);
        return;
    }

    stop_live_streams();

    if (uses_gnome_live_previews)
    {
        std::vector<GnomeLivePreviewRect> previews;
        previews.reserve(m_window_ids.size());

        const int global_x =
            m_monitor_geometry.x +
            m_applied_position.x;
        const int global_y =
            m_monitor_geometry.y +
            m_applied_position.y;

        for (std::size_t index = 0;
             index < m_window_ids.size();
             ++index)
        {
            const auto target =
                m_thumbnail_targets.find(
                    m_window_ids[index]);
            if (target == m_thumbnail_targets.end())
                continue;

            previews.push_back({
                target->first,
                global_x + m_size.padding +
                    static_cast<int>(index) *
                        (m_size.card_width + m_size.gap),
                global_y + WINDOW_PADDING +
                    m_size.header_height,
                target->second.target_width,
                target->second.target_height});
        }

        m_thumbnail_provider.show_gnome_live_previews(
            previews,
            std::move(callback));
        m_live_window_ids = desired_windows;

        // Most previews are painted directly by Shell. A compositor actor can
        // nevertheless be temporarily unavailable while a window maps,
        // minimizes, or changes workspace. After the live-clone fade has had
        // time to settle, capture one cached frame beneath it. The delay is
        // cancelled when the pointer merely passes over a dock item, avoiding
        // the burst of Shell.Screenshot requests that immediate parallel
        // capture caused.
        m_gnome_thumbnail_fallback.disconnect();
        m_gnome_thumbnail_fallback =
            Glib::signal_timeout().connect(
                [this, generation]()
                {
                    if (generation != m_generation ||
                        !get_visible())
                    {
                        return false;
                    }

                    for (const auto &entry :
                         m_thumbnail_targets)
                    {
                        if (!entry.second.has_thumbnail)
                        {
                            request_thumbnail(
                                entry.first,
                                generation);
                        }
                    }

                    return false;
                },
                GNOME_FALLBACK_CAPTURE_DELAY_MS);

        return;
    }

    if (!m_uses_layer_shell)
    {
        const bool uses_muffin_full_live_capture =
            uses_muffin_session();
        m_live_window_ids = desired_windows;

        g_message(
            "Live X11 media thumbnails started: windows=%zu; interval=%ums",
            m_live_window_ids.size(),
            X11_LIVE_REFRESH_MS);

        m_x11_live_refresh =
            Glib::signal_timeout().connect(
                [this,
                 generation,
                 uses_muffin_full_live_capture]()
                {
                    if (generation != m_generation ||
                        !m_dynamic_refresh ||
                        !get_visible())
                    {
                        return false;
                    }

                    const auto now =
                        g_get_monotonic_time();

                    for (const auto &window_id :
                         m_live_window_ids)
                    {
                        const auto target =
                            m_thumbnail_targets.find(
                                window_id);

                        if (target == m_thumbnail_targets.end())
                            continue;

                        const bool matches_media_title =
                            !m_media_title.empty() &&
                            target->second.caption.find(
                                m_media_title) !=
                                std::string::npos;
                        const bool recently_changed =
                            target->second.live_until_us >
                            now;

                        // Muffin keeps mapped current-workspace pixmaps live.
                        // Capture every such card directly: relying on a
                        // downscaled change probe can miss video motion and
                        // leave only some cards in a browser group frozen.
                        if (uses_muffin_full_live_capture ||
                            target->second.application_auxiliary ||
                            target->second.active ||
                            matches_media_title ||
                            recently_changed)
                        {
                            request_live_x11_thumbnail(
                                window_id,
                                generation,
                                matches_media_title ||
                                    recently_changed);
                        }
                    }

                    return true;
                },
                X11_LIVE_REFRESH_MS);

        // Direct Muffin capture already covers every eligible card. Avoid a
        // redundant low-resolution request competing for the same pixmap.
        if (uses_muffin_full_live_capture)
            return;

        m_x11_probe_refresh =
            Glib::signal_timeout().connect(
                [this, generation]()
                {
                    if (generation != m_generation ||
                        !m_dynamic_refresh ||
                        !get_visible())
                    {
                        return false;
                    }

                    for (const auto &window_id :
                         m_live_window_ids)
                    {
                        request_x11_change_probe(
                            window_id,
                            generation);
                    }

                    return true;
                },
                X11_CHANGE_PROBE_MS);

        return;
    }

    for (const auto &window_id : desired_windows)
    {
        const auto target =
            m_thumbnail_targets.find(window_id);

        if (target == m_thumbnail_targets.end())
            continue;

        g_message(
            "Live media thumbnail: MPRIS title='%s'; window='%s'; id=%s",
            m_media_title.c_str(),
            target->second.caption.c_str(),
            window_id.c_str());

        if (m_stream_provider.start(
                window_id,
                target->second.target_width,
                target->second.target_height,
                [this, generation](
                    const WindowId &completed_window_id,
                    const Glib::RefPtr<Gdk::Pixbuf> &frame)
                {
                    if (generation != m_generation || !frame)
                        return;

                    const auto completed =
                        m_thumbnail_targets.find(
                            completed_window_id);

                    if (completed == m_thumbnail_targets.end())
                        return;

                    auto &thumbnail = completed->second;
                    const auto signature =
                        pixbuf_signature(frame);

                    if (!thumbnail.has_live_signature)
                    {
                        thumbnail.has_live_signature = true;
                        thumbnail.live_signature = signature;
                        return;
                    }

                    if (signature == thumbnail.live_signature)
                        return;

                    thumbnail.live_signature = signature;
                    thumbnail.image->set(
                        scaled_to_fit(
                            frame,
                            thumbnail.target_width,
                            thumbnail.target_height));
                    thumbnail.image->queue_draw();
                    m_thumbnail_cache[completed_window_id] =
                        frame;
                    thumbnail.has_thumbnail = true;
                }))
        {
            m_live_window_ids.insert(window_id);
        }
        else
        {
            g_warning(
                "Cannot start live media thumbnail for window %s",
                window_id.c_str());
        }
    }
}

void DockPreviewWindow::stop_live_streams(
    bool animate_gnome_hide)
{
    if (!m_replacing_gnome_wayland_preview)
    {
        m_thumbnail_provider.hide_gnome_live_previews(
            animate_gnome_hide);
    }
    m_gnome_thumbnail_fallback.disconnect();
    m_x11_live_refresh.disconnect();
    m_x11_probe_refresh.disconnect();
    m_stream_provider.stop_all();
    m_live_window_ids.clear();
}
