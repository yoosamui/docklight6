// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_preview_window.cpp
//
// Implementation overview:
// Implements the interactive horizontal preview surface for an
// application's window group.
//
// Important implementation decisions:
// - Static thumbnails and live streams share the same card update path.
// - Preview placement follows monitor bounds and dock orientation.
// - Window activation and close actions are delegated to the application
//   controller.
//
// ------------------------------------------------------------

#include "dock_preview_window.h"

#include "dock/dock_constants.h"

#include <gtk-layer-shell.h>
#include <glib/gstdio.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <sstream>

namespace
{

    constexpr int HEADER_HEIGHT = 32;
    constexpr int CLOSE_BUTTON_SIZE = 16;
    constexpr unsigned int X11_LIVE_REFRESH_MS = 33;
    constexpr unsigned int X11_STATIC_RETRY_MS = 80;
    constexpr unsigned int X11_STATIC_RETRY_COUNT = 8;
    constexpr unsigned int XFWM_RECOVERY_SETTLE_MS = 500;
    constexpr unsigned int X11_CHANGE_PROBE_MS = 200;
    constexpr std::int64_t X11_LIVE_GRACE_US = 750000;
    constexpr int X11_PROBE_WIDTH = 96;
    constexpr int X11_PROBE_HEIGHT = 54;
    constexpr double X11_LIVE_OVERSAMPLE = 1.5;
    constexpr double CARD_CORNER_RADIUS = 7.0;
    constexpr double PREVIEW_PI =
        3.14159265358979323846;

    double ease_opacity(
        double progress,
        bool hiding)
    {
        progress = std::clamp(
            progress,
            0.0,
            1.0);

        return hiding
            ? progress * progress * progress
            : 1.0 - std::pow(1.0 - progress, 3.0);
    }

    ScreenPosition animation_offset(
        DockLocation location)
    {
        constexpr int distance = 28;

        switch (location)
        {
        case DockLocation::top:
            return {0, -distance};
        case DockLocation::bottom:
            return {0, distance};
        case DockLocation::left:
            return {-distance, 0};
        case DockLocation::right:
            return {distance, 0};
        }

        return {};
    }

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
        const ApplicationWindowEntry &entry)
    {
        const auto start_time =
            process_start_time(entry.process_id);
        if (entry.id.empty() ||
            entry.process_id <= 0 ||
            start_time.empty())
        {
            return {};
        }

        return sha256(
            "docklight-xfwm-thumbnail-v1\n" +
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

    void append_rounded_rectangle(
        const Cairo::RefPtr<Cairo::Context>
            &context,
        double width,
        double height,
        double radius)
    {
        const double effective_radius =
            std::max(
                0.0,
                std::min(
                    radius,
                    std::min(width, height) /
                        2.0));

        context->begin_new_sub_path();
        context->arc(
            width - effective_radius,
            effective_radius,
            effective_radius,
            -PREVIEW_PI / 2.0,
            0.0);
        context->arc(
            width - effective_radius,
            height - effective_radius,
            effective_radius,
            0.0,
            PREVIEW_PI / 2.0);
        context->arc(
            effective_radius,
            height - effective_radius,
            effective_radius,
            PREVIEW_PI / 2.0,
            PREVIEW_PI);
        context->arc(
            effective_radius,
            effective_radius,
            effective_radius,
            PREVIEW_PI,
            3.0 * PREVIEW_PI / 2.0);
        context->close_path();
    }

    struct PreviewMetrics
    {
        int card_width = DockPreviewWindow::CARD_WIDTH;
        int gap = DockPreviewWindow::CARD_GAP;
        int padding = DockPreviewWindow::WINDOW_PADDING;
        int header_height = HEADER_HEIGHT;
        int width = 0;
        int height = 0;
    };

    std::string desktop_badge_text(
        const std::vector<unsigned int>
            &desktop_numbers)
    {
        std::string text = "[ ";

        for (std::size_t index = 0;
             index < desktop_numbers.size();
             ++index)
        {
            if (index > 0)
                text += ", ";

            text += std::to_string(
                desktop_numbers[index]);
        }

        text += " ]";
        return text;
    }

    std::string preview_title(
        const ApplicationWindowEntry &entry)
    {
        auto title = entry.caption.empty()
                         ? entry.id
                         : entry.caption;

        // Match the dynamic context menu: current-workspace windows need no
        // badge, while windows from other workspaces identify their location.
        if (!entry.on_current_desktop &&
            !entry.desktop_numbers.empty())
        {
            title = desktop_badge_text(
                        entry.desktop_numbers) +
                    " " + title;
        }

        return title;
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

        if (width == source->get_width() &&
            height == source->get_height())
        {
            return source;
        }

        return source->scale_simple(
            width,
            height,
            Gdk::INTERP_BILINEAR);
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

    int thumbnail_height_for(
        const ApplicationWindowEntry &entry,
        int card_width)
    {
        const auto &geometry = entry.frame_geometry;

        if (geometry.width <= 0 ||
            geometry.height <= 0)
        {
            return std::max(1, card_width);
        }

        return std::clamp(
            static_cast<int>(std::lround(
                static_cast<double>(card_width) *
                geometry.height /
                geometry.width)),
            1,
            DockPreviewWindow::MAX_HEIGHT);
    }

    int automatic_preview_height(
        const std::vector<ApplicationWindowEntry>
            &entries)
    {
        int image_height = 1;

        for (const auto &entry : entries)
        {
            image_height = std::max(
                image_height,
                thumbnail_height_for(
                    entry,
                    DockPreviewWindow::CARD_WIDTH));
        }

        return std::clamp(
            HEADER_HEIGHT + image_height +
                2 * DockPreviewWindow::WINDOW_PADDING,
            DockPreviewWindow::MIN_HEIGHT,
            DockPreviewWindow::MAX_HEIGHT);
    }

    PreviewMetrics preview_metrics(
        const std::vector<ApplicationWindowEntry>
            &entries,
        int available_width,
        int available_height,
        int card_user_height)
    {
        PreviewMetrics metrics;
        const auto window_count = entries.size();

        if (window_count == 0 ||
            available_width <= 0 ||
            available_height <= 0)
        {
            return metrics;
        }

        const int automatic_height =
            automatic_preview_height(entries);
        const int requested_height =
            card_user_height ==
                    DockPreviewWindow::CARD_USER_HEIGHT
                ? automatic_height
                : std::clamp(
                      card_user_height,
                      DockPreviewWindow::MIN_HEIGHT,
                      DockPreviewWindow::MAX_HEIGHT);
        // The configured value fixes the vertical preview/card boundary. The
        // title border stays at HEADER_HEIGHT, while card width follows the new
        // thumbnail-area height so the image really resizes without distortion.
        // Horizontal monitor fitting may reduce that derived width afterwards.
        metrics.height = std::max(
            1,
            std::min(
                available_height,
                requested_height));
        const int automatic_image_height =
            std::max(
                1,
                automatic_height -
                    HEADER_HEIGHT -
                    2 * DockPreviewWindow::WINDOW_PADDING);
        const int requested_image_height =
            std::max(
                1,
                metrics.height -
                    HEADER_HEIGHT -
                    2 * DockPreviewWindow::WINDOW_PADDING);
        const double thumbnail_scale =
            static_cast<double>(requested_image_height) /
            automatic_image_height;

        metrics.card_width = std::max(
            1,
            static_cast<int>(std::lround(
                DockPreviewWindow::CARD_WIDTH *
                thumbnail_scale)));
        const int requested_card_width =
            metrics.card_width;

        const long long natural_width =
            2LL * metrics.padding +
            static_cast<long long>(window_count) *
                metrics.card_width +
            static_cast<long long>(window_count - 1) *
                metrics.gap;

        const double fit_scale = std::min(
            1.0,
            static_cast<double>(available_width) /
                natural_width);

        if (fit_scale < 1.0)
        {
            metrics.card_width = std::max(
                1,
                static_cast<int>(std::floor(
                    metrics.card_width * fit_scale)));
            metrics.gap = std::max(
                1,
                static_cast<int>(std::floor(
                    metrics.gap * fit_scale)));
        }

        auto allocated_width = [&]()
        {
            return 2LL * metrics.padding +
                   static_cast<long long>(window_count) *
                       metrics.card_width +
                   static_cast<long long>(window_count - 1) *
                       metrics.gap;
        };

        while (allocated_width() > available_width &&
               metrics.card_width > 1)
        {
            --metrics.card_width;
        }

        while (allocated_width() > available_width &&
               metrics.gap > 1)
        {
            --metrics.gap;
        }

        while (allocated_width() > available_width &&
               metrics.padding > 0)
        {
            --metrics.padding;
        }

        // Width fitting and thumbnail fitting are the same operation. Keep the
        // title border fixed, but shrink the thumbnail area and the toplevel
        // height by the card-width scale actually achieved after rounding.
        const double achieved_scale =
            static_cast<double>(metrics.card_width) /
            requested_card_width;
        const int fitted_image_height = std::max(
            1,
            static_cast<int>(std::floor(
                requested_image_height *
                achieved_scale)));
        const int minimum_height = std::min(
            available_height,
            DockPreviewWindow::MIN_HEIGHT);
        metrics.height = std::clamp(
            metrics.header_height +
                2 * DockPreviewWindow::WINDOW_PADDING +
                fitted_image_height,
            minimum_height,
            available_height);

        metrics.width = static_cast<int>(
            std::min<long long>(
                available_width,
                allocated_width()));

        return metrics;
    }

}

DockPreviewWindow::DockPreviewWindow()
{
    set_decorated(false);
    set_resizable(false);
    set_accept_focus(false);
    set_focus_on_map(false);

    add_events(
        Gdk::ENTER_NOTIFY_MASK |
        Gdk::LEAVE_NOTIFY_MASK);

    m_row.set_margin_start(WINDOW_PADDING);
    m_row.set_margin_end(WINDOW_PADDING);
    m_row.set_margin_top(WINDOW_PADDING);
    m_row.set_margin_bottom(WINDOW_PADDING);
    m_row.set_halign(Gtk::ALIGN_START);
    m_row.set_valign(Gtk::ALIGN_START);

    m_scroller.set_policy(
        Gtk::POLICY_NEVER,
        Gtk::POLICY_NEVER);
    m_scroller.add(m_row);
    m_surface.add(m_scroller);
    add(m_surface);

    get_style_context()->add_class(
        "dock-preview-window");
    m_surface.get_style_context()->add_class(
        "dock-preview");

    m_css = Gtk::CssProvider::create();
    get_style_context()->add_provider(
        m_css,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    m_surface.get_style_context()->add_provider(
        m_css,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    m_corner_css = Gtk::CssProvider::create();
    m_surface.get_style_context()->add_provider(
        m_corner_css,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    m_css->load_from_data(
        "window.dock-preview-window {"
        " background-color: transparent;"
        "}"
        ".dock-preview {"
        " background-color: rgba(28, 28, 32, 0.96);"
        " border: 1px solid rgba(255,255,255,0.28);"
        "}"
        ".dock-preview-card {"
        " background: transparent;"
        " border: 1px solid rgba(255,255,255,0.25);"
        " border-radius: 7px;"
        "}"
        ".dock-preview-card.dock-preview-card-selected {"
        " border-color: rgba(105,170,255,0.85);"
        "}"
        ".dock-preview-header {"
        " border-bottom: 1px solid rgba(255,255,255,0.18);"
        "}"
        ".dock-preview-title {"
        " color: white; font-size: 11px;"
        "}"
        ".dock-preview-close {"
        " min-width: 0; min-height: 0;"
        " padding: 0; border: 0;"
        " background: transparent; color: white;"
        "}"
        ".dock-preview-close:hover {"
        " background: rgba(220,60,60,0.9);"
        " border-radius: 11px;"
        "}");

    set_rounded_corners(true, 10);

    auto *window = GTK_WINDOW(gobj());
    m_uses_layer_shell = gtk_layer_is_supported();

    if (m_uses_layer_shell)
    {
        gtk_layer_init_for_window(window);
        gtk_layer_set_namespace(
            window,
            "docklight6-preview");
        gtk_layer_set_layer(
            window,
            GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_keyboard_mode(
            window,
            GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
        gtk_layer_set_exclusive_zone(window, 0);
    }
    else
    {
        set_type_hint(Gdk::WINDOW_TYPE_HINT_UTILITY);
        set_skip_taskbar_hint(true);
        set_skip_pager_hint(true);
        set_keep_above(true);
        stick();
        set_position(Gtk::WIN_POS_NONE);
    }

    signal_map().connect(
        [this]()
        {
            if (m_has_position)
            {
                apply_position(
                    m_location,
                    m_position,
                    m_size.width,
                    m_size.height);
            }
        });
}

DockPreviewWindow::~DockPreviewWindow()
{
    cancel_opacity_animation();
    m_thumbnail_cache_refresh.disconnect();
    m_thumbnail_recovery_delay.disconnect();
    for (auto &retry : m_thumbnail_cache_retries)
        retry.second.disconnect();
    m_thumbnail_cache_retries.clear();
    stop_live_streams();
    ++m_generation;

    // Keep the most recent active frames for the next DockLight start. Use a
    // copy because a successful write removes the id from the dirty set.
    const auto dirty_thumbnails =
        m_thumbnail_cache_dirty;
    for (const auto &window_id : dirty_thumbnails)
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

    clear_cards();
}

void DockPreviewWindow::set_monitor(
    const Glib::RefPtr<Gdk::Monitor> &monitor)
{
    if (monitor)
    {
        Gdk::Rectangle geometry;
        monitor->get_geometry(geometry);
        m_monitor_geometry = {
            geometry.get_x(),
            geometry.get_y(),
            geometry.get_width(),
            geometry.get_height()};
    }

    if (m_uses_layer_shell)
    {
        gtk_layer_set_monitor(
            GTK_WINDOW(gobj()),
            monitor ? monitor->gobj() : nullptr);
    }
}

void DockPreviewWindow::set_card_user_height(
    int height)
{
    m_card_user_height =
        height == CARD_USER_HEIGHT ||
                (height >= MIN_HEIGHT &&
                 height <= MAX_HEIGHT)
            ? height
            : CARD_USER_HEIGHT;
}

void DockPreviewWindow::set_rounded_corners(
    bool enabled,
    int radius)
{
    auto context =
        m_surface.get_style_context();

    if (enabled)
        context->add_class("dock-rounded");
    else
        context->remove_class("dock-rounded");

    const int effective_radius =
        enabled
            ? std::min(
                  std::max(0, radius),
                  MIN_HEIGHT / 2)
            : 0;

    m_corner_css->load_from_data(
        ".dock-preview { border-radius: " +
        std::to_string(effective_radius) +
        "px; }");
}

void DockPreviewWindow::prime_thumbnail_cache(
    const std::vector<ApplicationWindowEntry>
        &entries)
{
    if (!m_uses_layer_shell)
    {
        const bool xfwm_session =
            uses_xfwm_session();
        const auto previously_active =
            m_thumbnail_cache_active;
        std::set<WindowId> known_window_ids;
        std::set<WindowId> eligible_window_ids;
        std::set<WindowId> active_window_ids;
        for (const auto &entry : entries)
        {
            if (!entry.id.empty())
            {
                known_window_ids.insert(entry.id);

                if (xfwm_session)
                {
                    const auto identity =
                        persistent_thumbnail_identity(
                            entry);
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

                    if (m_thumbnail_cache.count(
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

                }

                if (!xfwm_session ||
                    (!entry.minimized &&
                     entry.on_current_desktop))
                {
                    eligible_window_ids.insert(
                        entry.id);

                    if (xfwm_session &&
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

        // Capture a newly active Xfwm window once. Refreshing it continuously
        // while the preview is closed keeps XComposite busy at idle; visible
        // previews already have their own demand-driven live refresh path.
        for (const auto &window_id :
             m_thumbnail_cache_active)
        {
            request_active_cache_refresh(window_id);
        }

        for (const auto &entry : entries)
        {
            if (entry.id.empty() ||
                m_thumbnail_cache_eligible.count(
                    entry.id) == 0 ||
                entry.id ==
                    m_thumbnail_recovery_active ||
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

        m_thumbnail_cache_refresh.disconnect();
    }
}

void DockPreviewWindow::request_cached_thumbnail(
    const WindowId &window_id,
    unsigned int retries_remaining)
{
    if (window_id.empty() ||
        m_thumbnail_cache.count(window_id) != 0 ||
        m_thumbnail_cache_in_flight.count(
            window_id) != 0 ||
        m_thumbnail_cache_eligible.count(
            window_id) == 0)
    {
        return;
    }

    m_thumbnail_cache_in_flight.insert(window_id);

    // Remember a compositor-scaled frame while Xfwm still exposes the
    // mapped window pixmap. The registry can announce a new window slightly
    // before that pixmap exists, so retry cache priming just like the visible
    // preview capture. Once minimized or moved off-workspace, the last valid
    // frame remains available without reading Xfwm's unmapped backing store.
    m_thumbnail_provider.request(
        window_id,
        MAX_HEIGHT * 2,
        MAX_HEIGHT,
        [this, retries_remaining](
            const WindowId &completed_window_id,
            const Glib::RefPtr<Gdk::Pixbuf> &thumbnail)
        {
            m_thumbnail_cache_in_flight.erase(
                completed_window_id);

            if (thumbnail &&
                m_known_window_ids.count(
                    completed_window_id) != 0 &&
                m_thumbnail_cache_eligible.count(
                    completed_window_id) != 0)
            {
                // A capture already in flight when recovery presented the
                // window can finish while Xfwm is still painting it. Only
                // the request started after the settle delay may complete an
                // icon recovery.
                if (m_thumbnail_recovery_active ==
                        completed_window_id &&
                    m_thumbnail_recovery_capture_allowed.count(
                        completed_window_id) == 0)
                {
                    return;
                }

                m_thumbnail_recovery_capture_allowed.erase(
                    completed_window_id);
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
                return;
            }

            auto &retry =
                m_thumbnail_cache_retries[
                    completed_window_id];
            retry.disconnect();
            retry = Glib::signal_timeout().connect(
                [this,
                 completed_window_id,
                 retries_remaining]()
                {
                    request_cached_thumbnail(
                        completed_window_id,
                        retries_remaining - 1);
                    return false;
                },
                X11_STATIC_RETRY_MS);
        },
        1.0,
        m_thumbnail_recovery_capture_allowed.count(
            window_id) != 0,
        uses_xfwm_session());
}

void DockPreviewWindow::request_active_cache_refresh(
    const WindowId &window_id)
{
    if (window_id.empty() ||
        m_thumbnail_cache_active.count(window_id) == 0 ||
        m_thumbnail_cache_in_flight.count(
            window_id) != 0 ||
        m_thumbnail_recovery_requested.count(
            window_id) != 0 ||
        (m_dynamic_refresh &&
         visible_for(window_id)))
    {
        return;
    }

    m_thumbnail_cache_in_flight.insert(window_id);

    // Xfwm has no readable backing pixmap for hidden workspaces in this
    // session. Refresh only the active, mapped window: it is safe to use the
    // guarded native fallback, and the resulting frame becomes the last-known
    // image shown after minimize or a workspace switch.
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
        true,
        uses_xfwm_session());
}

void DockPreviewWindow::persist_thumbnail_cache(
    const WindowId &window_id,
    const Glib::RefPtr<Gdk::Pixbuf> &thumbnail)
{
    if (!uses_xfwm_session() ||
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

DockPreviewSize DockPreviewWindow::preferred_size(
    const std::vector<ApplicationWindowEntry>
        &entries,
    int available_width,
    int available_height) const
{
    const auto metrics = preview_metrics(
        entries,
        available_width,
        available_height,
        m_card_user_height);

    return {
        metrics.width,
        metrics.height,
        metrics.card_width,
        metrics.gap,
        metrics.padding,
        metrics.header_height};
}

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

    cancel_opacity_animation();
    stop_live_streams();
    m_media_title.clear();
    m_live_window_ids.clear();
    m_dynamic_refresh = false;
    ++m_generation;
    rebuild(entries, size);
    set_size_request(size.width, size.height);
    set_default_size(size.width, size.height);
    m_location = location;
    m_position = position;
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
    apply_position(
        location,
        position,
        size.width,
        size.height);

    // Keep the first frame visible so xfwm4 maps the overlay, then ease it
    // from the edge into its final position and opacity.
    set_opacity(0.18);

    show_all();
    queue_resize();
    start_opacity_animation(false);
}

void DockPreviewWindow::hide_preview()
{
    stop_live_streams();
    m_media_title.clear();
    m_live_window_ids.clear();
    m_dynamic_refresh = false;

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
    stop_live_streams();
    m_media_title.clear();
    m_live_window_ids.clear();
    m_dynamic_refresh = false;

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

void DockPreviewWindow::start_opacity_animation(
    bool hiding)
{
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
    m_animation_moves_window =
        !m_uses_layer_shell &&
        m_has_position;

    if (m_animation_moves_window)
    {
        const auto offset =
            animation_offset(
                m_location);
        const int visible_x =
            m_monitor_geometry.x +
            m_position.x;
        const int visible_y =
            m_monitor_geometry.y +
            m_position.y;
        const int hidden_x =
            visible_x + offset.x;
        const int hidden_y =
            visible_y + offset.y;

        if (hiding)
        {
            int current_x = visible_x;
            int current_y = visible_y;
            get_position(current_x, current_y);
            m_animation_start_x = current_x;
            m_animation_start_y = current_y;
            m_animation_target_x = hidden_x;
            m_animation_target_y = hidden_y;
        }
        else
        {
            m_animation_start_x = hidden_x;
            m_animation_start_y = hidden_y;
            m_animation_target_x = visible_x;
            m_animation_target_y = visible_y;
            move(
                m_animation_start_x,
                m_animation_start_y);
        }
    }

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
    const double eased =
        ease_opacity(
            progress,
            m_opacity_animation_hiding);
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

    if (m_animation_moves_window)
    {
        const int x = static_cast<int>(
            std::lround(
                m_animation_start_x +
                (m_animation_target_x -
                 m_animation_start_x) *
                    eased));
        const int y = static_cast<int>(
            std::lround(
                m_animation_start_y +
                (m_animation_target_y -
                 m_animation_start_y) *
                    eased));
        move(x, y);
    }

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
    else if (m_animation_moves_window)
    {
        move(
            m_animation_target_x,
            m_animation_target_y);
    }

    return false;
}

void DockPreviewWindow::set_dynamic_refresh(
    bool enabled,
    const std::string &media_title)
{
    // Firefox can leave its single browser-wide MPRIS player associated with
    // a different tab after a video enters Picture-in-Picture. A recognized
    // X11 application auxiliary is itself sufficient evidence that the
    // preview needs live frames; do not let stale MPRIS metadata freeze it.
    const bool has_x11_application_auxiliary =
        !m_uses_layer_shell &&
        std::any_of(
            m_thumbnail_targets.begin(),
            m_thumbnail_targets.end(),
            [](const auto &entry)
            {
                return !entry.second.minimized &&
                       entry.second.application_auxiliary;
            });

    m_dynamic_refresh =
        enabled || has_x11_application_auxiliary;

    if (!media_title.empty())
        m_media_title = media_title;

    if (!enabled)
        m_media_title.clear();

    if (m_dynamic_refresh &&
        get_visible() &&
        !m_thumbnail_targets.empty())
    {
        start_live_streams();
    }
    else
    {
        stop_live_streams();
    }
}

bool DockPreviewWindow::visible_for(
    const WindowId &window_id) const
{
    return get_visible() &&
           std::find(
               m_window_ids.begin(),
               m_window_ids.end(),
               window_id) != m_window_ids.end();
}

void DockPreviewWindow::rebuild(
    const std::vector<ApplicationWindowEntry>
        &entries,
    const DockPreviewSize &size)
{
    clear_cards();
    const auto generation = m_generation;

    m_row.set_spacing(size.gap);
    m_row.set_margin_start(size.padding);
    m_row.set_margin_end(size.padding);
    m_row.set_margin_top(WINDOW_PADDING);
    m_row.set_margin_bottom(WINDOW_PADDING);

    const int image_height =
        std::max(
            1,
            size.height - size.header_height -
                2 * WINDOW_PADDING);

    for (const auto &entry : entries)
    {
        auto card = new Gtk::EventBox();
        card->set_size_request(
            size.card_width,
            size.header_height + image_height);
        card->set_visible_window(true);
        card->add_events(
            Gdk::ENTER_NOTIFY_MASK |
            Gdk::LEAVE_NOTIFY_MASK |
            Gdk::POINTER_MOTION_MASK);
        card->get_style_context()->add_class(
            "dock-preview-card");

        auto selected =
            std::make_shared<bool>(false);
        auto background = Gtk::manage(
            new Gtk::DrawingArea());
        background->set_size_request(
            size.card_width,
            size.header_height + image_height);
        background->set_hexpand(true);
        background->set_vexpand(true);
        background->signal_draw().connect(
            [background, selected](
                const Cairo::RefPtr<Cairo::Context>
                    &context)
            {
                const auto allocation =
                    background->get_allocation();

                if (*selected)
                {
                    context->set_source_rgba(
                        105.0 / 255.0,
                        170.0 / 255.0,
                        1.0,
                        0.32);
                }
                else
                {
                    context->set_source_rgba(
                        1.0,
                        1.0,
                        1.0,
                        0.06);
                }

                append_rounded_rectangle(
                    context,
                    allocation.get_width(),
                    allocation.get_height(),
                    CARD_CORNER_RADIUS);
                context->fill();
                return true;
            });

        auto body = Gtk::manage(
            new Gtk::Box(
                Gtk::ORIENTATION_VERTICAL,
                0));
        auto header = Gtk::manage(
            new Gtk::Box(
                Gtk::ORIENTATION_HORIZONTAL,
                std::max(
                    1,
                    4 * size.header_height /
                        HEADER_HEIGHT)));
        header->set_size_request(
            -1,
            size.header_height);
        header->set_margin_start(
            std::max(
                1,
                6 * size.header_height /
                    HEADER_HEIGHT));
        header->set_margin_end(
            std::max(
                1,
                4 * size.header_height /
                    HEADER_HEIGHT));
        header->get_style_context()->add_class(
            "dock-preview-header");

        auto title = Gtk::manage(new Gtk::Label());
        title->set_text(preview_title(entry));
        title->set_ellipsize(
            Pango::ELLIPSIZE_END);
        title->set_halign(Gtk::ALIGN_START);
        title->set_hexpand(true);
        title->set_tooltip_text(title->get_text());
        title->get_style_context()->add_class(
            "dock-preview-title");

        auto close = Gtk::manage(new Gtk::EventBox());
        close->set_size_request(
            CLOSE_BUTTON_SIZE,
            CLOSE_BUTTON_SIZE);
        close->set_halign(Gtk::ALIGN_CENTER);
        close->set_valign(Gtk::ALIGN_CENTER);
        close->set_hexpand(false);
        close->set_vexpand(false);
        close->set_visible_window(true);
        close->add_events(
            Gdk::BUTTON_RELEASE_MASK);
        close->set_tooltip_text("Close window");
        close->get_style_context()->add_class(
            "dock-preview-close");

        auto close_glyph = Gtk::manage(
            new Gtk::DrawingArea());
        close_glyph->set_size_request(
            CLOSE_BUTTON_SIZE,
            CLOSE_BUTTON_SIZE);
        close_glyph->signal_draw().connect(
            [](const Cairo::RefPtr<Cairo::Context>
                   &context)
            {
                constexpr char GLYPH[] = "×";

                context->select_font_face(
                    "Sans",
                    Cairo::FONT_SLANT_NORMAL,
                    Cairo::FONT_WEIGHT_NORMAL);
                context->set_font_size(
                    CLOSE_BUTTON_SIZE);

                Cairo::TextExtents extents;
                context->get_text_extents(
                    GLYPH,
                    extents);
                context->move_to(
                    (CLOSE_BUTTON_SIZE -
                     extents.width) /
                            2.0 -
                        extents.x_bearing,
                    (CLOSE_BUTTON_SIZE -
                     extents.height) /
                            2.0 -
                        extents.y_bearing);
                context->set_source_rgb(
                    1.0,
                    1.0,
                    1.0);
                context->show_text(GLYPH);
                return true;
            });
        close->add(*close_glyph);
        close->signal_button_release_event().connect(
            [this, window_id = entry.id](
                GdkEventButton *event)
            {
                if (event && event->button == 1)
                {
                    m_close_window.emit(window_id);
                    return true;
                }

                return false;
            });

        header->pack_start(*title, true, true);
        header->pack_end(*close, false, false);

        auto image = Gtk::manage(new Gtk::Image());
        image->set_size_request(
            size.card_width,
            image_height);
        image->set_halign(Gtk::ALIGN_CENTER);
        image->set_valign(Gtk::ALIGN_CENTER);

        auto image_event = Gtk::manage(
            new Gtk::EventBox());
        image_event->set_visible_window(false);
        image_event->set_size_request(
            size.card_width,
            image_height);
        image_event->add(*image);
        image_event->add_events(
            Gdk::BUTTON_RELEASE_MASK);
        image_event->signal_button_release_event().connect(
            [this, window_id = entry.id](
                GdkEventButton *event)
            {
                if (event && event->button == 1)
                {
                    m_activate_window.emit(window_id);
                    return true;
                }

                return false;
            });

        card->signal_enter_notify_event().connect(
            [card,
             background,
             selected](GdkEventCrossing *event)
            {
                if (!event ||
                    event->detail !=
                        GDK_NOTIFY_INFERIOR)
                {
                    *selected = true;
                    card->get_style_context()
                        ->add_class(
                            "dock-preview-card-selected");
                    background->queue_draw();
                }

                return false;
            });
        card->signal_motion_notify_event().connect(
            [card,
             background,
             selected](GdkEventMotion *)
            {
                if (!*selected)
                {
                    *selected = true;
                    card->get_style_context()
                        ->add_class(
                            "dock-preview-card-selected");
                    background->queue_draw();
                }

                return false;
            });
        card->signal_leave_notify_event().connect(
            [card,
             background,
             selected](GdkEventCrossing *event)
            {
                if (!event ||
                    event->detail !=
                        GDK_NOTIFY_INFERIOR)
                {
                    *selected = false;
                    card->get_style_context()
                        ->remove_class(
                            "dock-preview-card-selected");
                    background->queue_draw();
                }

                return false;
            });

        body->pack_start(*header, false, false);
        body->pack_start(*image_event, true, true);

        auto card_overlay = Gtk::manage(
            new Gtk::Overlay());
        card_overlay->set_size_request(
            size.card_width,
            size.header_height + image_height);
        card_overlay->add(*background);
        card_overlay->add_overlay(*body);
        card->add(*card_overlay);

        m_row.pack_start(*card, false, false);
        m_cards.push_back(card);
        m_window_ids.push_back(entry.id);

        const int fallback_size = std::max(
            1,
            std::min(
                size.card_width,
                image_height));
        const auto fallback_icon =
            entry.icon_name.empty()
                ? std::string{
                      "application-x-executable"}
                : entry.icon_name;

        const auto target = m_thumbnail_targets.emplace(
            entry.id,
            ThumbnailTarget{
                image,
                fallback_icon,
                fallback_size,
                size.card_width,
                image_height,
                entry.caption,
                entry.active,
                entry.minimized,
                entry.on_current_desktop,
                entry.application_auxiliary,
                false,
                false,
                false,
                0,
                false,
                false,
                0,
                0,
                0}).first;

        // Cards are recreated whenever a preview is reopened. Seed the new
        // Gtk::Image with the last successfully captured frame so a transient
        // XComposite failure cannot flash the application icon.
        const auto cached =
            m_thumbnail_cache.find(entry.id);
        if (cached != m_thumbnail_cache.end() &&
            cached->second)
        {
            image->set(
                scaled_to_fit(
                    cached->second,
                    size.card_width,
                    image_height));
            target->second.has_thumbnail = true;
        }

        request_thumbnail(
            entry.id,
            generation);
    }
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

    // Xfwm unmaps minimized and off-workspace windows. A named composite
    // pixmap for an unmapped window can expose stale backing storage from a
    // different client. Keep only a frame captured while this window was
    // mapped; otherwise show its own application icon.
    if (uses_xfwm_session() &&
        (target->second.minimized ||
         !target->second.on_current_desktop))
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
        [this, generation](
            const WindowId &completed_window_id,
            const Glib::RefPtr<Gdk::Pixbuf>
                &thumbnail)
        {
            if (generation != m_generation)
                return;

            const auto completed =
                m_thumbnail_targets.find(
                    completed_window_id);

            if (completed ==
                m_thumbnail_targets.end())
            {
                return;
            }

            auto &target = completed->second;
            target.capture_in_flight = false;

            if (thumbnail &&
                (!uses_xfwm_session() ||
                 m_thumbnail_cache_eligible.count(
                     completed_window_id) != 0))
            {
                m_thumbnail_cache[completed_window_id] =
                    thumbnail;
                m_thumbnail_cache_dirty.insert(
                    completed_window_id);
                target.image->set(thumbnail);
                target.image->queue_draw();
                target.has_thumbnail = true;
                m_thumbnail_recovery_requested.erase(
                    completed_window_id);

                if (m_thumbnail_recovery_active ==
                    completed_window_id)
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
                         completed_window_id,
                         generation]()
                        {
                            if (generation == m_generation)
                            {
                                request_thumbnail(
                                    completed_window_id,
                                    generation);
                            }

                            return false;
                        },
                        X11_STATIC_RETRY_MS);
                    return;
                }

                // An icon is shown only when the initial static capture fails.
                // A live stream keeps this static image until its first frame.
                show_thumbnail_fallback(
                    completed_window_id);
            }
        },
        2.0,
        false,
        uses_xfwm_session());
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
                XFWM_RECOVERY_SETTLE_MS);
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
        found->second.minimized ||
        (uses_xfwm_session() &&
         !found->second.on_current_desktop))
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

            if (!frame)
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
        uses_xfwm_session());
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
        found->second.minimized ||
        (uses_xfwm_session() &&
         !found->second.on_current_desktop))
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

            if (!frame)
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
            thumbnail.image->set(frame);
            thumbnail.image->queue_draw();
            m_thumbnail_cache[completed_window_id] = frame;
            m_thumbnail_cache_dirty.insert(
                completed_window_id);
            thumbnail.has_thumbnail = true;
        },
        X11_LIVE_OVERSAMPLE,
        allow_xfwm_media_fallback,
        uses_xfwm_session());
}

void DockPreviewWindow::start_live_streams()
{
    const auto generation = m_generation;
    std::set<WindowId> desired_windows;

    for (const auto &entry : m_thumbnail_targets)
    {
        // Firefox exposes one browser-wide MPRIS player and can leave it
        // associated with an unrelated tab. Refresh the complete visible
        // group on X11 so a playing page cannot be omitted by stale metadata.
        if (!entry.second.minimized &&
            (!uses_xfwm_session() ||
             entry.second.on_current_desktop))
            desired_windows.insert(entry.first);
    }

    if (desired_windows == m_live_window_ids)
        return;

    stop_live_streams();

    if (!m_uses_layer_shell)
    {
        m_live_window_ids = desired_windows;

        g_message(
            "Live X11 media thumbnails started: windows=%zu; interval=%ums",
            m_live_window_ids.size(),
            X11_LIVE_REFRESH_MS);

        m_x11_live_refresh =
            Glib::signal_timeout().connect(
                [this, generation]()
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

                        if (target->second.application_auxiliary ||
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
                    thumbnail.image->set(frame);
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

void DockPreviewWindow::stop_live_streams()
{
    m_x11_live_refresh.disconnect();
    m_x11_probe_refresh.disconnect();
    m_stream_provider.stop_all();
    m_live_window_ids.clear();
}

void DockPreviewWindow::clear_cards()
{
    stop_live_streams();
    m_media_title.clear();
    m_live_window_ids.clear();
    m_thumbnail_cache_refresh.disconnect();

    for (auto &retry : m_thumbnail_cache_retries)
        retry.second.disconnect();
    m_thumbnail_cache_retries.clear();

    m_thumbnail_targets.clear();
    m_thumbnail_cache_active.clear();
    m_thumbnail_cache_in_flight.clear();
    m_thumbnail_recovery_requested.clear();
    m_thumbnail_recovery_queue.clear();
    m_thumbnail_recovery_active.clear();
    m_thumbnail_recovery_capture_allowed.clear();
    m_thumbnail_recovery_delay.disconnect();
    m_window_ids.clear();

    for (auto *card : m_cards)
    {
        m_row.remove(*card);
        delete card;
    }

    m_cards.clear();
}

void DockPreviewWindow::apply_position(
    DockLocation location,
    const ScreenPosition &position,
    int width,
    int height)
{
    if (!m_uses_layer_shell)
    {
        move(
            m_monitor_geometry.x + position.x,
            m_monitor_geometry.y + position.y);
        return;
    }

    auto *window = GTK_WINDOW(gobj());
    const bool right =
        location == DockLocation::right;
    const bool bottom =
        location == DockLocation::bottom;

    gtk_layer_set_anchor(
        window,
        GTK_LAYER_SHELL_EDGE_LEFT,
        !right);
    gtk_layer_set_anchor(
        window,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        right);
    gtk_layer_set_anchor(
        window,
        GTK_LAYER_SHELL_EDGE_TOP,
        !bottom);
    gtk_layer_set_anchor(
        window,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        bottom);

    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_LEFT,
        right ? 0 : position.x);
    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        right
            ? std::max(
                  0,
                  m_monitor_geometry.width -
                      position.x - width)
            : 0);
    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_TOP,
        bottom ? 0 : position.y);
    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        bottom
            ? std::max(
                  0,
                  m_monitor_geometry.height -
                      position.y - height)
            : 0);
}

bool DockPreviewWindow::on_enter_notify_event(
    GdkEventCrossing *event)
{
    if (!event ||
        event->detail != GDK_NOTIFY_INFERIOR)
    {
        m_pointer_entered.emit();
    }

    return Gtk::Window::on_enter_notify_event(event);
}

bool DockPreviewWindow::on_leave_notify_event(
    GdkEventCrossing *event)
{
    if (!event ||
        event->detail != GDK_NOTIFY_INFERIOR)
    {
        m_pointer_left.emit();
    }

    return Gtk::Window::on_leave_notify_event(event);
}

sigc::signal<void> &
DockPreviewWindow::signal_pointer_entered()
{
    return m_pointer_entered;
}

sigc::signal<void> &
DockPreviewWindow::signal_pointer_left()
{
    return m_pointer_left;
}

sigc::signal<void, const WindowId &> &
DockPreviewWindow::signal_activate_window()
{
    return m_activate_window;
}

sigc::signal<void, const WindowId &> &
DockPreviewWindow::signal_reload_thumbnail()
{
    return m_reload_thumbnail;
}

sigc::signal<void, const WindowId &> &
DockPreviewWindow::signal_close_window()
{
    return m_close_window;
}
