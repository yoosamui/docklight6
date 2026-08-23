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
#include "presentation/docklight_surface_identity.h"

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
    constexpr unsigned int GNOME_FALLBACK_CAPTURE_DELAY_MS = 500;
    constexpr unsigned int GNOME_PREVIEW_REMAP_DELAY_MS = 34;
    constexpr unsigned int GNOME_PREVIEW_REVEAL_DELAY_MS = 50;
    constexpr unsigned int THUMBNAIL_RECOVERY_SETTLE_MS = 500;
    constexpr unsigned int X11_CHANGE_PROBE_MS = 200;
    constexpr std::int64_t X11_LIVE_GRACE_US = 750000;
    constexpr int X11_PROBE_WIDTH = 96;
    constexpr int X11_PROBE_HEIGHT = 54;
    constexpr double X11_LIVE_OVERSAMPLE = 1.5;
    constexpr double CARD_CORNER_RADIUS = 7.0;
    constexpr double PREVIEW_PI =
        3.14159265358979323846;

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

    bool uses_wayland_session()
    {
        const char *session_type =
            g_getenv("XDG_SESSION_TYPE");
        return session_type &&
               g_ascii_strcasecmp(
                   session_type,
                   "wayland") == 0;
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

class DockPreviewCardCanvas : public Gtk::DrawingArea
{
public:
    DockPreviewCardCanvas(
        int width,
        int header_height,
        int image_height,
        const Gdk::RGBA &preview_color)
        : m_header_height(header_height),
          m_image_height(image_height),
          m_preview_color(preview_color)
    {
        set_size_request(
            width,
            header_height + image_height);
    }

    void set_selected(bool selected)
    {
        if (m_selected == selected)
            return;

        m_selected = selected;
        queue_draw();
    }

    void set_preview_color(
        const Gdk::RGBA &preview_color)
    {
        m_preview_color = preview_color;
        queue_draw();
    }

    void set(
        const Glib::RefPtr<Gdk::Pixbuf> &pixbuf)
    {
        m_pixbuf = pixbuf;
        queue_draw();
    }

    void set_pixel_size(int size)
    {
        m_fallback_size = size;
    }

    void set_from_icon_name(
        const std::string &name,
        Gtk::IconSize)
    {
        try
        {
            m_pixbuf = Gtk::IconTheme::get_default()
                           ->load_icon(
                               name,
                               std::max(1, m_fallback_size),
                               Gtk::ICON_LOOKUP_FORCE_SIZE);
        }
        catch (const Glib::Error &)
        {
            m_pixbuf.reset();
        }

        queue_draw();
    }

protected:
    bool on_draw(
        const Cairo::RefPtr<Cairo::Context>
            &context) override
    {
        const auto allocation = get_allocation();
        const int width = allocation.get_width();
        const int height = allocation.get_height();

        context->set_source_rgba(
            m_selected
                ? m_preview_color.get_red()
                : 1.0,
            m_selected
                ? m_preview_color.get_green()
                : 1.0,
            m_selected
                ? m_preview_color.get_blue()
                : 1.0,
            m_selected
                ? 0.32 * m_preview_color.get_alpha()
                : 0.06);
        append_rounded_rectangle(
            context,
            width,
            height,
            CARD_CORNER_RADIUS);
        context->fill();

        // The thumbnail and selector are deliberately painted in this same
        // Cairo pass. The opaque image-area base prevents the selector from
        // showing through capture alpha, and the pixbuf is always painted
        // after the selector.
        context->rectangle(
            0,
            m_header_height,
            width,
            m_image_height);
        context->set_source_rgb(
            28.0 / 255.0,
            28.0 / 255.0,
            32.0 / 255.0);
        context->fill();

        if (m_pixbuf)
        {
            const double x =
                (width - m_pixbuf->get_width()) / 2.0;
            const double y =
                m_header_height +
                (m_image_height -
                 m_pixbuf->get_height()) /
                    2.0;
            Gdk::Cairo::set_source_pixbuf(
                context,
                m_pixbuf,
                x,
                y);
            context->paint();
        }

        return true;
    }

private:
    Glib::RefPtr<Gdk::Pixbuf> m_pixbuf;
    int m_header_height = 0;
    int m_image_height = 0;
    int m_fallback_size = 1;
    Gdk::RGBA m_preview_color;
    bool m_selected = false;
};

DockPreviewWindow::DockPreviewWindow()
{
    m_preview_color.set("#69aaff");

    set_decorated(false);
    set_resizable(false);
    set_app_paintable(true);
    set_accept_focus(false);
    set_focus_on_map(false);
    gtk_window_set_role(
        GTK_WINDOW(gobj()),
        DocklightSurfaceIdentity::PREVIEW_ROLE);

    // CSS rounds the child surface, not the native X11 toplevel. Ensure the
    // pixels outside that surface can actually be transparent on X11 by
    // selecting an RGBA visual before realization and clearing the complete
    // toplevel on every draw.
    auto screen = get_screen();

    if (screen)
    {
        auto rgba_visual = screen->get_rgba_visual();

        if (rgba_visual)
        {
            gtk_widget_set_visual(
                GTK_WIDGET(gobj()),
                rgba_visual->gobj());
        }
    }

    signal_draw().connect(
        [](const Cairo::RefPtr<Cairo::Context> &context)
        {
            context->save();
            context->set_operator(Cairo::OPERATOR_SOURCE);
            context->set_source_rgba(0.0, 0.0, 0.0, 0.0);
            context->paint();
            context->restore();
            return false;
        },
        false);

    add_events(
        Gdk::ENTER_NOTIFY_MASK |
        Gdk::POINTER_MOTION_MASK |
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
            DocklightSurfaceIdentity::
                PREVIEW_NAMESPACE);
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
                const auto allocation =
                    get_allocation();
                apply_allocated_position(
                    std::max(
                        1,
                        allocation.get_width()),
                    std::max(
                        1,
                        allocation.get_height()));
            }
        });

    signal_size_allocate().connect(
        [this](Gtk::Allocation &allocation)
        {
            if (!m_has_position || !get_visible())
                return;

            apply_allocated_position(
                std::max(
                    1,
                    allocation.get_width()),
                std::max(
                    1,
                    allocation.get_height()));

            complete_presentation();
        });
}

DockPreviewWindow::~DockPreviewWindow()
{
    cancel_opacity_animation();
    m_gnome_preview_remap_delay.disconnect();
    m_gnome_preview_reveal_delay.disconnect();
    m_thumbnail_cache_refresh.disconnect();
    m_thumbnail_recovery_delay.disconnect();
    for (auto &retry : m_thumbnail_cache_retries)
        retry.second.disconnect();
    m_thumbnail_cache_retries.clear();
    for (auto &delay : m_thumbnail_cache_settle_delays)
        delay.second.disconnect();
    m_thumbnail_cache_settle_delays.clear();
    m_replacing_gnome_wayland_preview = false;
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
        m_workarea_geometry = {
            0,
            0,
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

void DockPreviewWindow::set_workarea_geometry(
    const MonitorGeometry &geometry)
{
    if (geometry.width > 0 &&
        geometry.height > 0)
    {
        m_workarea_geometry = geometry;
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

void DockPreviewWindow::set_preview_color(
    const std::string &color)
{
    Gdk::RGBA parsed;
    if (!parsed.set(color))
        parsed.set("#69aaff");

    m_preview_color = parsed;

    for (auto &entry : m_thumbnail_targets)
    {
        auto &target = entry.second;
        if (target.image)
            target.image->set_preview_color(
                m_preview_color);
    }

    if (m_thumbnail_provider
            .supports_gnome_live_previews())
    {
        m_thumbnail_provider.set_gnome_preview_color(
            m_preview_color.get_red(),
            m_preview_color.get_green(),
            m_preview_color.get_blue(),
            m_preview_color.get_alpha());
    }
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

void DockPreviewWindow::set_thumbnail_policy(
    WindowThumbnailPolicy policy)
{
    m_thumbnail_policy = policy;
    m_thumbnail_provider.set_x11_window_redirection(
        uses_redirected_thumbnail_capture());
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
                       entry.second.on_current_desktop &&
                       entry.second.application_auxiliary;
            });

    // Browser MPRIS state is only a hint: it can be absent, delayed, or tied
    // to another tab. Muffin keeps mapped current-workspace pixmaps live, so
    // keep the refresh scheduler active for those windows and capture them
    // directly below. Hidden compositor pixmaps are allowed to remain frozen.
    const bool has_visible_current_muffin_target =
        !m_uses_layer_shell &&
        uses_muffin_session() &&
        !m_thumbnail_provider.supports_gnome_live_previews() &&
        std::any_of(
            m_thumbnail_targets.begin(),
            m_thumbnail_targets.end(),
            [](const auto &entry)
            {
                return !entry.second.minimized &&
                       entry.second.on_current_desktop;
            });

    m_dynamic_refresh =
        enabled ||
        has_x11_application_auxiliary ||
        has_visible_current_muffin_target;

    if (!media_title.empty())
        m_media_title = media_title;

    if (!enabled)
        m_media_title.clear();

    if (m_thumbnail_provider.supports_gnome_live_previews() &&
        get_visible() &&
        !m_thumbnail_targets.empty())
    {
        start_live_streams();
    }
    else if (m_dynamic_refresh &&
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

void DockPreviewWindow::set_input_forwarding(
    bool forwarding)
{
    m_input_forwarding = forwarding;
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
    const bool uses_gnome_live_previews =
        m_thumbnail_provider
            .supports_gnome_live_previews();

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

        auto image = Gtk::manage(
            new DockPreviewCardCanvas(
                size.card_width,
                size.header_height,
                image_height,
                m_preview_color));

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
                    const bool last_card =
                        !m_window_ids.empty() &&
                        m_window_ids.back() == window_id;
                    m_close_pointer_origin_valid =
                        last_card;
                    m_close_pointer_root_x = event->x_root;
                    m_close_pointer_root_y = event->y_root;
                    m_close_window.emit(
                        window_id,
                        last_card);
                    return true;
                }

                return false;
            });

        header->pack_start(*title, true, true);
        header->pack_end(*close, false, false);

        auto image_event = Gtk::manage(
            new Gtk::EventBox());
        image_event->set_visible_window(false);
        image_event->set_size_request(
            size.card_width,
            image_height);
        image_event->add_events(
            Gdk::BUTTON_PRESS_MASK |
            Gdk::BUTTON_RELEASE_MASK);
        const bool forwards_live_preview_click =
            entry.application_auxiliary &&
            m_thumbnail_provider
                .supports_gnome_live_previews();
        image_event->signal_button_press_event().connect(
            [forwards_live_preview_click](
                GdkEventButton *event)
            {
                if (!event ||
                    event->button != 1 ||
                    !forwards_live_preview_click)
                {
                    return false;
                }

                // Consume the press, but wait for release before asking Shell
                // to inject the PiP click. Mutter retains an implicit pointer
                // grab for the duration of the physical press.
                return true;
            });
        image_event->signal_button_release_event().connect(
            [this,
             image_event,
             window_id = entry.id,
             forwards_live_preview_click](
                GdkEventButton *event)
            {
                if (event && event->button == 1)
                {
                    if (forwards_live_preview_click)
                    {
                        const auto allocation =
                            image_event->get_allocation();
                        const double width = std::max(
                            1,
                            allocation.get_width());
                        const double height = std::max(
                            1,
                            allocation.get_height());
                        m_thumbnail_provider
                            .forward_gnome_preview_primary_click(
                                window_id,
                                event->x / width,
                                event->y / height);
                        return true;
                    }

                    m_activate_window.emit(window_id);
                    return true;
                }

                return false;
            });

        card->signal_enter_notify_event().connect(
            [this, card, image](GdkEventCrossing *)
            {
                if (m_input_forwarding)
                    return false;

                if (m_selected_card != card)
                {
                    auto *previous = m_selected_card;
                    auto *previous_canvas =
                        m_selected_canvas;
                    m_selected_card = card;
                    m_selected_canvas = image;

                    if (previous)
                    {
                        previous->get_style_context()
                            ->remove_class(
                                "dock-preview-card-selected");
                        previous->queue_draw();
                    }

                    if (previous_canvas)
                        previous_canvas->set_selected(false);

                    image->set_selected(true);
                    card->get_style_context()
                        ->add_class(
                            "dock-preview-card-selected");
                    card->queue_draw();
                }

                return false;
            });
        card->signal_motion_notify_event().connect(
            [this, card, image](GdkEventMotion *)
            {
                if (m_input_forwarding)
                    return false;

                if (m_selected_card != card)
                {
                    auto *previous = m_selected_card;
                    auto *previous_canvas =
                        m_selected_canvas;
                    m_selected_card = card;
                    m_selected_canvas = image;

                    if (previous)
                    {
                        previous->get_style_context()
                            ->remove_class(
                                "dock-preview-card-selected");
                        previous->queue_draw();
                    }

                    if (previous_canvas)
                        previous_canvas->set_selected(false);

                    image->set_selected(true);
                    card->get_style_context()
                        ->add_class(
                            "dock-preview-card-selected");
                    card->queue_draw();
                }

                return false;
            });
        card->signal_leave_notify_event().connect(
            [this, card, image](GdkEventCrossing *event)
            {
                if (m_input_forwarding)
                    return false;

                if (!event ||
                    event->detail !=
                        GDK_NOTIFY_INFERIOR)
                {
                    if (m_selected_card == card)
                    {
                        m_selected_card = nullptr;
                        m_selected_canvas = nullptr;
                        image->set_selected(false);
                        card->get_style_context()
                            ->remove_class(
                                "dock-preview-card-selected");
                        card->queue_draw();
                    }
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
        card_overlay->add(*image);
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

        if (uses_gnome_live_previews)
        {
            // Shell paints the compositor texture over this card as soon as
            // ShowLivePreviews is handled. Requesting a PNG of the same
            // window in parallel is redundant and, when the pointer crosses
            // several dock items quickly, can queue enough asynchronous
            // Shell.Screenshot work to make the compositor unresponsive.
            // Keep a cached frame or cheap icon beneath the short live-preview
            // fade instead.
            show_thumbnail_fallback(entry.id);
        }
        else
        {
            request_thumbnail(
                entry.id,
                generation);
        }
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

void DockPreviewWindow::stop_live_streams()
{
    if (!m_replacing_gnome_wayland_preview)
        m_thumbnail_provider.hide_gnome_live_previews();
    m_gnome_thumbnail_fallback.disconnect();
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
    m_thumbnail_candidate_signatures.clear();
    m_thumbnail_recovery_delay.disconnect();
    m_window_ids.clear();
    m_selected_card = nullptr;
    m_selected_canvas = nullptr;

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
    m_applied_position = position;

    if (!m_uses_layer_shell)
    {
        const int global_x =
            m_monitor_geometry.x + position.x;
        const int global_y =
            m_monitor_geometry.y + position.y;

        // GNOME Wayland ignores client-requested toplevel coordinates. The
        // Shell integration consumes this private title payload and moves the
        // preview after Mutter creates its surface.
        set_title(
            "Docklight 6 Preview@" +
            std::to_string(global_x) + "," +
            std::to_string(global_y));

        // Apply mapped X11/XWayland geometry atomically. Separate move and
        // resize requests can expose the previous allocation for one frame.
        const auto gdk_window = get_window();

        if (gdk_window)
        {
            gdk_window->move_resize(
                global_x,
                global_y,
                width,
                height);
        }
        else
        {
            move(global_x, global_y);
        }
        return;
    }

    auto *window = GTK_WINDOW(gobj());
    const auto layer_position =
        overlay_position_in_workarea(
            position,
            m_workarea_geometry);
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
        right
            ? 0
            : std::max(0, layer_position.x));
    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        right
            ? std::max(
                  0,
                  m_workarea_geometry.width -
                      layer_position.x - width)
            : 0);
    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_TOP,
        bottom
            ? 0
            : std::max(0, layer_position.y));
    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        bottom
            ? std::max(
                  0,
                  m_workarea_geometry.height -
                      layer_position.y - height)
            : 0);
}

void DockPreviewWindow::apply_allocated_position(
    int width,
    int height)
{
    const auto position =
        overlay_position_for_allocation(
            m_location,
            m_position,
            m_size.width,
            m_size.height,
            width,
            height);
    const bool position_changed =
        position.x != m_applied_position.x ||
        position.y != m_applied_position.y;

    apply_position(
        m_location,
        position,
        width,
        height);

    // GNOME Shell paints live compositor actors at absolute card positions.
    // If GTK reports its final allocation after the actors were installed,
    // publish the corrected origin as well as moving the GTK surface.
    if (position_changed &&
        m_thumbnail_provider
            .supports_gnome_live_previews() &&
        !m_live_window_ids.empty())
    {
        m_live_window_ids.clear();
        start_live_streams();
    }
}

bool DockPreviewWindow::on_enter_notify_event(
    GdkEventCrossing *event)
{
    if (m_input_forwarding)
        return false;

    if (!event ||
        event->detail != GDK_NOTIFY_INFERIOR)
    {
        m_close_pointer_origin_valid = false;
        m_pointer_entered.emit();
    }

    return Gtk::Window::on_enter_notify_event(event);
}

bool DockPreviewWindow::on_motion_notify_event(
    GdkEventMotion *event)
{
    if (!m_input_forwarding &&
        (!m_close_pointer_origin_valid ||
         std::abs(event->x_root - m_close_pointer_root_x) > 0.5 ||
         std::abs(event->y_root - m_close_pointer_root_y) > 0.5))
    {
        m_close_pointer_origin_valid = false;
        m_pointer_moved.emit();
    }

    return Gtk::Window::on_motion_notify_event(event);
}

bool DockPreviewWindow::on_leave_notify_event(
    GdkEventCrossing *event)
{
    if (m_input_forwarding)
        return false;

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
DockPreviewWindow::signal_pointer_moved()
{
    return m_pointer_moved;
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

sigc::signal<void, const WindowId &, bool> &
DockPreviewWindow::signal_close_window()
{
    return m_close_window;
}
