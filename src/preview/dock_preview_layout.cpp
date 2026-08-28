// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_preview_layout.cpp
//
// Implementation overview:
// Implements preview sizing, card construction, and surface placement.
//
// ------------------------------------------------------------

#include "dock_preview_window.h"
#include "dock_preview_window_internal.h"
#include "integrations/desktop_session_identity.h"

#include <gtk-layer-shell.h>
#include <glib/gstdio.h>

#include <algorithm>
#include <cmath>

namespace
{

    constexpr int HEADER_HEIGHT = 32;
    constexpr int CLOSE_BUTTON_SIZE = 16;
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

} // namespace

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

        if (DesktopSessionIdentity::
                is_gnome_wayland_session())
        {
            // GNOME Wayland ignores client-requested toplevel coordinates.
            // Its Shell integration consumes this private title payload.
            set_title(
                "Docklight 6 Preview@" +
                std::to_string(global_x) + "," +
                std::to_string(global_y));
        }
        else
        {
            // A coordinate payload would make an installed GNOME extension
            // mistake this native X11 utility for a Shell-owned surface and
            // suppress its compositor actor. X11 placement is handled below.
            set_title("Docklight 6 Preview");
        }

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
