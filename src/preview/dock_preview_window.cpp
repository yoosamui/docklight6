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

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace
{

    constexpr int HEADER_HEIGHT = 32;
    constexpr int CLOSE_BUTTON_SIZE = 16;
    constexpr double CARD_CORNER_RADIUS = 7.0;
    constexpr double PREVIEW_PI =
        3.14159265358979323846;

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

DockPreviewWindow::~DockPreviewWindow()
{
    m_stream_provider.stop_all();
    ++m_generation;
    clear_cards();
}

void DockPreviewWindow::set_monitor(
    const Glib::RefPtr<Gdk::Monitor> &monitor)
{
    gtk_layer_set_monitor(
        GTK_WINDOW(gobj()),
        monitor ? monitor->gobj() : nullptr);
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

    m_stream_provider.stop_all();
    m_media_title.clear();
    m_live_window_ids.clear();
    m_dynamic_refresh = false;
    ++m_generation;
    rebuild(entries, size);
    set_size_request(size.width, size.height);
    set_default_size(size.width, size.height);
    apply_position(location, position);
    show_all();

    // set_size_request() only changes the toplevel's minimum requisition.
    // A mapped layer-shell surface otherwise retains the previous group's
    // allocation when the new preview is smaller. Explicitly request the
    // calculated monitor-constrained allocation so the window itself, not
    // only its thumbnail children, shrinks or grows.
    resize(size.width, size.height);
    queue_resize();
}

void DockPreviewWindow::hide_preview()
{
    m_stream_provider.stop_all();
    m_media_title.clear();
    m_live_window_ids.clear();
    m_dynamic_refresh = false;
    ++m_generation;
    hide();
    clear_cards();
}

void DockPreviewWindow::set_dynamic_refresh(
    bool enabled,
    const std::string &media_title)
{
    m_dynamic_refresh = enabled;

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
        m_stream_provider.stop_all();
        m_live_window_ids.clear();
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

        m_thumbnail_targets.emplace(
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
                false,
                false,
                false,
                0});

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

            if (thumbnail && !target.has_thumbnail)
            {
                target.image->set(thumbnail);
                target.has_thumbnail = true;
            }
            else if (!target.has_thumbnail)
            {
                // An icon is shown only when the initial static capture fails.
                // A live stream keeps this static image until its first frame.
                target.image->set_pixel_size(
                    target.fallback_size);
                target.image->set_from_icon_name(
                    target.fallback_icon,
                    Gtk::ICON_SIZE_DIALOG);
            }
        });
}

void DockPreviewWindow::start_live_streams()
{
    const auto generation = m_generation;
    std::set<WindowId> desired_windows;

    for (const auto &entry : m_thumbnail_targets)
    {
        if (!entry.second.minimized)
            desired_windows.insert(entry.first);
    }

    if (desired_windows == m_live_window_ids)
        return;

    m_stream_provider.stop_all();
    m_live_window_ids.clear();

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

void DockPreviewWindow::clear_cards()
{
    m_stream_provider.stop_all();
    m_media_title.clear();
    m_live_window_ids.clear();
    m_thumbnail_targets.clear();
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
    const ScreenPosition &position)
{
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
        right ? position.x : 0);
    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_TOP,
        bottom ? 0 : position.y);
    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        bottom ? position.y : 0);
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
DockPreviewWindow::signal_close_window()
{
    return m_close_window;
}
