// Interactive horizontal window-group preview surface.

#include "dock_preview_window.h"

#include "dock_constants.h"

#include <gtk-layer-shell.h>

#include <algorithm>
#include <cmath>
#include <memory>

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
    add(m_scroller);

    get_style_context()->add_class(
        "dock-preview-window");

    m_css = Gtk::CssProvider::create();
    get_style_context()->add_provider(
        m_css,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    m_css->load_from_data(
        "window.dock-preview-window {"
        " background: rgba(28, 28, 32, 0.96);"
        " border: 1px solid rgba(255,255,255,0.28);"
        " border-radius: 10px;"
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
    ++m_generation;
    hide();
    clear_cards();
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

                context->rectangle(
                    0.0,
                    0.0,
                    allocation.get_width(),
                    allocation.get_height());
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
        m_images[entry.id] = image;
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

        m_thumbnail_provider.request(
            entry.id,
            size.card_width,
            image_height,
            [this,
             generation,
             fallback_icon,
             fallback_size](
                const WindowId &window_id,
                const Glib::RefPtr<Gdk::Pixbuf>
                    &thumbnail)
            {
                if (generation != m_generation)
                    return;

                const auto image =
                    m_images.find(window_id);

                if (image != m_images.end())
                {
                    if (thumbnail)
                    {
                        image->second->set(thumbnail);
                    }
                    else
                    {
                        image->second->set_pixel_size(
                            fallback_size);
                        image->second->set_from_icon_name(
                            fallback_icon,
                            Gtk::ICON_SIZE_DIALOG);
                    }
                }
            });
    }
}

void DockPreviewWindow::clear_cards()
{
    m_images.clear();
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
