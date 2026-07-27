#include "dock_item.h"
#include "dock_layout_metrics.h"
#include "dock_window.h"

#include <algorithm>
#include <string>
#include <vector>

namespace
{

constexpr int ZOOM_FRAME_COUNT = 9;
constexpr unsigned int ZOOM_FRAME_INTERVAL_MS = 16;
constexpr int BLUR_FRAME_COUNT = 9;
constexpr unsigned int BLUR_FRAME_INTERVAL_MS = 16;
constexpr int BLUR_OUTER_RED = 105;
constexpr int BLUR_OUTER_GREEN = 170;
constexpr int BLUR_OUTER_BLUE = 255;
constexpr int BLUR_INNER_RED = 235;
constexpr int BLUR_INNER_GREEN = 245;
constexpr int BLUR_INNER_BLUE = 255;
constexpr int BLUR_OUTER_MAX_ALPHA = 225;
constexpr int BLUR_INNER_MAX_ALPHA = 190;

Glib::RefPtr<Gdk::Pixbuf>
create_transparent_pixbuf(
    int width,
    int height)
{
    auto pixbuf =
        Gdk::Pixbuf::create(
            Gdk::COLORSPACE_RGB,
            true,
            8,
            std::max(1, width),
            std::max(1, height));

    pixbuf->fill(0x00000000);

    return pixbuf;
}

std::vector<float>
box_blur_alpha(
    const std::vector<float> &source,
    int width,
    int height,
    int radius)
{
    if (radius <= 0)
        return source;

    const int diameter =
        radius * 2 + 1;

    std::vector<float> horizontal(
        source.size(),
        0.0F);

    std::vector<float> result(
        source.size(),
        0.0F);

    for (int y = 0; y < height; ++y)
    {
        float sum = 0.0F;

        for (int offset = -radius;
             offset <= radius;
             ++offset)
        {
            if (offset >= 0 &&
                offset < width)
            {
                sum +=
                    source[
                        static_cast<std::size_t>(
                            y * width + offset)];
            }
        }

        for (int x = 0; x < width; ++x)
        {
            horizontal[
                static_cast<std::size_t>(
                    y * width + x)] =
                sum / diameter;

            const int remove_x =
                x - radius;

            const int add_x =
                x + radius + 1;

            if (remove_x >= 0)
            {
                sum -=
                    source[
                        static_cast<std::size_t>(
                            y * width +
                            remove_x)];
            }

            if (add_x < width)
            {
                sum +=
                    source[
                        static_cast<std::size_t>(
                            y * width +
                            add_x)];
            }
        }
    }

    for (int x = 0; x < width; ++x)
    {
        float sum = 0.0F;

        for (int offset = -radius;
             offset <= radius;
             ++offset)
        {
            if (offset >= 0 &&
                offset < height)
            {
                sum +=
                    horizontal[
                        static_cast<std::size_t>(
                            offset * width + x)];
            }
        }

        for (int y = 0; y < height; ++y)
        {
            result[
                static_cast<std::size_t>(
                    y * width + x)] =
                sum / diameter;

            const int remove_y =
                y - radius;

            const int add_y =
                y + radius + 1;

            if (remove_y >= 0)
            {
                sum -=
                    horizontal[
                        static_cast<std::size_t>(
                            remove_y * width +
                            x)];
            }

            if (add_y < height)
            {
                sum +=
                    horizontal[
                        static_cast<std::size_t>(
                            add_y * width +
                            x)];
            }
        }
    }

    return result;
}

std::vector<float>
blur_alpha(
    std::vector<float> alpha,
    int width,
    int height,
    int radius)
{
    for (int pass = 0; pass < 3; ++pass)
    {
        alpha =
            box_blur_alpha(
                alpha,
                width,
                height,
                radius);
    }

    return alpha;
}

Glib::RefPtr<Gdk::Pixbuf>
create_blur_pixbuf(
    const std::vector<float> &alpha,
    int size,
    int red,
    int green,
    int blue)
{
    auto blur =
        create_transparent_pixbuf(
            size,
            size);

    auto *pixels =
        blur->get_pixels();

    const int rowstride =
        blur->get_rowstride();

    for (int y = 0; y < size; ++y)
    {
        auto *row =
            pixels + y * rowstride;

        for (int x = 0; x < size; ++x)
        {
            auto *pixel =
                row + x * 4;

            pixel[0] =
                static_cast<guchar>(red);
            pixel[1] =
                static_cast<guchar>(green);
            pixel[2] =
                static_cast<guchar>(blue);
            pixel[3] =
                static_cast<guchar>(
                    std::clamp(
                        alpha[
                            static_cast<std::size_t>(
                                y * size + x)],
                        0.0F,
                        255.0F));
        }
    }

    return blur;
}

}

DockItem::DockItem(
    DockWindow &dock,
    Glib::RefPtr<Gio::AppInfo> app,
    int icon_size,
    DockHoverEffect hover_effect)
    : m_dock(dock),
      m_app(app),
      m_hover_effect(hover_effect)
{
    set_visible_window(false);

    add_events(
        Gdk::ENTER_NOTIFY_MASK |
        Gdk::LEAVE_NOTIFY_MASK |
        Gdk::BUTTON_PRESS_MASK);

    image.set_halign(Gtk::ALIGN_CENTER);
    image.set_valign(Gtk::ALIGN_CENTER);
    add(image);

    signal_popup_menu().connect(
        sigc::mem_fun(
            *this,
            &DockItem::on_popup_menu));

    initialize_context_menu();
    set_icon_size(icon_size);

    show_all_children();
}

DockItem::~DockItem()
{
    m_zoom_animation.disconnect();
    m_blur_animation.disconnect();
}

void DockItem::set_icon_size(int icon_size)
{
    icon_size = std::max(1, icon_size);

    if (icon_size == m_icon_size)
        return;

    m_icon_size = icon_size;

    reload_icon();

    set_size_request(
        DockLayoutMetrics::item_size_for(icon_size),
        DockLayoutMetrics::item_size_for(icon_size));
}

void DockItem::set_hover_effect(
    DockHoverEffect effect)
{
    if (effect == m_hover_effect)
    {
        apply_hover_effect();
        return;
    }

    m_hover_effect = effect;

    if (m_hover_effect == DockHoverEffect::zoom)
    {
        create_zoom_frames();
    }
    else
    {
        m_zoom_animation.disconnect();
        m_zoom_frames.clear();
        m_zoom_frame = 0;
        m_zoom_target_frame = 0;
    }

    if (m_hover_effect == DockHoverEffect::blur)
    {
        create_blur_frames();
    }
    else
    {
        m_blur_animation.disconnect();
        m_blur_frames.clear();
        m_blur_frame = 0;
        m_blur_target_frame = 0;
    }

    apply_hover_effect();
}

void DockItem::set_context_menu_corner_radius(
    int corner_radius)
{
    if (!m_context_menu_css)
        return;

    m_context_menu_css->load_from_data(
        "window.dock-context-menu-popup,"
        "window.dock-context-menu-popup decoration {"
        " background-color: transparent;"
        " background-image: none;"
        " border-radius: " +
        std::to_string(
            std::max(0, corner_radius)) +
        "px;"
        "}"
        "menu.dock-context-menu {"
        " background-clip: padding-box;"
        " border-radius: " +
        std::to_string(
            std::max(0, corner_radius)) +
        "px;"
        "}");
}

void DockItem::reload_icon()
{
    auto icon = m_app->get_icon();
    auto icon_theme =
        Gtk::IconTheme::get_default();

    if (!icon_theme)
    {
        g_warning(
            "Cannot load icon for %s: no GTK icon theme",
            m_app->get_name().c_str());
        return;
    }

    Gtk::IconInfo icon_info;

    if (icon)
    {
        icon_info =
            icon_theme->lookup_icon(
                icon,
                m_icon_size,
                Gtk::ICON_LOOKUP_USE_BUILTIN);
    }

    if (!icon_info)
    {
        icon_info =
            icon_theme->lookup_icon(
                "application-x-executable",
                m_icon_size,
                Gtk::ICON_LOOKUP_USE_BUILTIN);
    }

    if (!icon_info)
    {
        g_warning(
            "Cannot find an icon for %s in the current theme",
            m_app->get_name().c_str());
        return;
    }

    try
    {
        auto pixbuf =
            icon_info.load_icon();

        if (!pixbuf)
        {
            g_warning(
                "Cannot load icon for %s from the current theme",
                m_app->get_name().c_str());
            return;
        }

        const int pixbuf_width =
            pixbuf->get_width();

        const int pixbuf_height =
            pixbuf->get_height();

        if (pixbuf_width > m_icon_size ||
            pixbuf_height > m_icon_size)
        {
            const double scale =
                std::min(
                    static_cast<double>(m_icon_size) /
                        pixbuf_width,
                    static_cast<double>(m_icon_size) /
                        pixbuf_height);

            pixbuf =
                pixbuf->scale_simple(
                    std::max(
                        1,
                        static_cast<int>(
                            pixbuf_width * scale)),
                    std::max(
                        1,
                        static_cast<int>(
                            pixbuf_height * scale)),
                    Gdk::INTERP_BILINEAR);
        }

        m_icon_pixbuf = pixbuf;
        m_hover_pixbuf =
            create_standard_hover_pixbuf(
                m_icon_pixbuf);

        if (m_hover_effect == DockHoverEffect::zoom)
        {
            create_zoom_frames();
        }
        else
        {
            m_zoom_animation.disconnect();
            m_zoom_frames.clear();
            m_zoom_frame = 0;
            m_zoom_target_frame = 0;
        }

        if (m_hover_effect == DockHoverEffect::blur)
        {
            create_blur_frames();
        }
        else
        {
            m_blur_animation.disconnect();
            m_blur_frames.clear();
            m_blur_frame = 0;
            m_blur_target_frame = 0;
        }

        apply_hover_effect();
    }
    catch (const Glib::Error &error)
    {
        // Keep the previously displayed pixbuf when the new theme contains
        // a broken icon. One bad asset must not leave an empty dock item.
        const auto error_message =
            error.what();

        g_warning(
            "Cannot reload icon for %s: %s",
            m_app->get_name().c_str(),
            error_message.c_str());
    }
}

Glib::ustring DockItem::app_name() const
{
    return m_app->get_display_name();
}

bool DockItem::on_enter_notify_event(
    GdkEventCrossing *)
{
    m_hovered = true;
    apply_hover_effect();

    m_dock.schedule_show_tooltip(*this);

    return true;
}

bool DockItem::on_leave_notify_event(
    GdkEventCrossing *)
{
    m_hovered = false;
    apply_hover_effect();

    m_dock.schedule_hide_tooltip();

    return false;
}

void DockItem::set_vertical(bool vertical)
{
    if (vertical)
        label.hide();
    else
        label.show();
}

bool DockItem::on_button_press_event(GdkEventButton *event)
{
    if (event->button == GDK_BUTTON_SECONDARY)
    {
        show_context_menu(
            reinterpret_cast<GdkEvent *>(event));
    }
    else if (event->button == GDK_BUTTON_PRIMARY)
    {
        try
        {
            std::vector<Glib::RefPtr<Gio::File>> files;

            m_app->launch(files);
        }
        catch (const Glib::Error &error)
        {
            g_warning(
                "Cannot launch %s: %s",
                m_app->get_name().c_str(),
                error.what().c_str());
        }
    }

    return true;
}

bool DockItem::on_popup_menu()
{
    show_context_menu(nullptr);
    return true;
}

void DockItem::initialize_context_menu()
{
    auto initialize_item =
        [](Gtk::MenuItem &item,
           unsigned int mnemonic_index)
        {
            item.set_halign(Gtk::ALIGN_FILL);
            item.set_valign(Gtk::ALIGN_CENTER);

            auto *label =
                dynamic_cast<Gtk::Label *>(
                    item.get_child());

            if (!label)
                return;

            label->set_halign(Gtk::ALIGN_FILL);
            label->set_valign(Gtk::ALIGN_FILL);
            label->set_xalign(0.0F);
            label->set_yalign(0.5F);

            // Preserve the native GTK mnemonic while ensuring its underline
            // remains visible even when the desktop hides mouse-opened menu
            // mnemonics.
            Pango::AttrList attributes;
            auto underline =
                Pango::Attribute::
                    create_attr_underline(
                        Pango::UNDERLINE_SINGLE);

            underline.set_start_index(
                mnemonic_index);
            underline.set_end_index(
                mnemonic_index + 1);
            attributes.insert(underline);
            label->set_attributes(attributes);
        };

    initialize_item(m_attach_item, 1);
    initialize_item(m_open_new_window_item, 0);
    initialize_item(m_minimize_item, 1);
    initialize_item(m_maximize_item, 1);
    initialize_item(m_close_all_item, 0);

    m_context_menu.append(
        m_attach_item);
    m_context_menu.append(
        m_attach_separator);
    m_context_menu.append(
        m_open_new_window_item);
    m_context_menu.append(
        m_window_separator);
    m_context_menu.append(
        m_minimize_item);
    m_context_menu.append(
        m_maximize_item);
    m_context_menu.append(
        m_close_separator);
    m_context_menu.append(
        m_close_all_item);

    m_attach_item
        .signal_activate()
        .connect(
            [this]()
            {
                log_context_action(
                    "Attach");
            });

    m_open_new_window_item
        .signal_activate()
        .connect(
            [this]()
            {
                log_context_action(
                    "Open New Window");
            });

    m_close_all_item
        .signal_activate()
        .connect(
            [this]()
            {
                log_context_action(
                    "Close All");
            });

    m_minimize_item
        .signal_activate()
        .connect(
            [this]()
            {
                log_context_action(
                    "Minimize");
            });

    m_maximize_item
        .signal_activate()
        .connect(
            [this]()
            {
                log_context_action(
                    "Maximize");
            });

    auto context =
        m_context_menu.get_style_context();

    context->add_class(
        "dock-context-menu");

    m_context_menu_css =
        Gtk::CssProvider::create();

    context->add_provider(
        m_context_menu_css,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION +
            1);

    m_context_menu.show_all();
}

void DockItem::show_context_menu(
    const GdkEvent *event)
{
    Gdk::Gravity widget_anchor =
        Gdk::GRAVITY_NORTH;

    Gdk::Gravity menu_anchor =
        Gdk::GRAVITY_SOUTH;

    switch (m_dock.location())
    {
    case DockLocation::bottom:
        widget_anchor = Gdk::GRAVITY_NORTH;
        menu_anchor = Gdk::GRAVITY_SOUTH;
        break;

    case DockLocation::top:
        widget_anchor = Gdk::GRAVITY_SOUTH;
        menu_anchor = Gdk::GRAVITY_NORTH;
        break;

    case DockLocation::left:
        widget_anchor = Gdk::GRAVITY_EAST;
        menu_anchor = Gdk::GRAVITY_WEST;
        break;

    case DockLocation::right:
        widget_anchor = Gdk::GRAVITY_WEST;
        menu_anchor = Gdk::GRAVITY_EAST;
        break;
    }

    m_dock.schedule_hide_tooltip();

    m_context_menu.popup_at_widget(
        this,
        widget_anchor,
        menu_anchor,
        event);

    // GtkMenu is hosted in its own popup GtkWindow. GTK normally hides
    // mnemonic underlines for pointer-opened menus, so make them visible on
    // that popup window after GTK has created and mapped it.
    auto *menu_toplevel =
        gtk_widget_get_toplevel(
            GTK_WIDGET(
                m_context_menu.gobj()));

    if (GTK_IS_WINDOW(menu_toplevel))
    {
        auto *popup_context =
            gtk_widget_get_style_context(
                menu_toplevel);

        gtk_style_context_add_class(
            popup_context,
            "dock-context-menu-popup");

        gtk_style_context_add_provider(
            popup_context,
            GTK_STYLE_PROVIDER(
                m_context_menu_css->gobj()),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION +
                1);

        gtk_window_set_mnemonics_visible(
            GTK_WINDOW(menu_toplevel),
            TRUE);
    }
}

void DockItem::log_context_action(
    const char *action) const
{
    g_message(
        "Dock context menu: %s (%s)",
        action,
        m_app->get_name().c_str());
}

void DockItem::apply_hover_effect()
{
    if (!m_icon_pixbuf)
        return;

    switch (m_hover_effect)
    {
    case DockHoverEffect::standard:
        m_zoom_animation.disconnect();
        m_zoom_frame = 0;
        m_zoom_target_frame = 0;
        m_blur_animation.disconnect();
        m_blur_frame = 0;
        m_blur_target_frame = 0;

        image.set(
            m_hovered && m_hover_pixbuf
                ? m_hover_pixbuf
                : m_icon_pixbuf);
        break;

    case DockHoverEffect::zoom:
        m_blur_animation.disconnect();
        m_blur_frame = 0;
        m_blur_target_frame = 0;

        if (m_zoom_frames.empty())
        {
            image.set(m_icon_pixbuf);
            return;
        }

        m_zoom_target_frame =
            m_hovered
                ? static_cast<int>(
                      m_zoom_frames.size()) -
                      1
                : 0;

        image.set(
            m_zoom_frames[
                static_cast<std::size_t>(
                    m_zoom_frame)]);

        start_zoom_animation();
        break;

    case DockHoverEffect::blur:
        m_zoom_animation.disconnect();
        m_zoom_frame = 0;
        m_zoom_target_frame = 0;

        if (m_blur_frames.empty())
        {
            image.set(m_icon_pixbuf);
            return;
        }

        m_blur_target_frame =
            m_hovered
                ? static_cast<int>(
                      m_blur_frames.size()) -
                      1
                : 0;

        image.set(
            m_blur_frames[
                static_cast<std::size_t>(
                    m_blur_frame)]);

        start_blur_animation();
        break;
    }
}

void DockItem::create_zoom_frames()
{
    m_zoom_animation.disconnect();
    m_zoom_frames.clear();
    m_zoom_frame = 0;
    m_zoom_target_frame = 0;

    if (!m_icon_pixbuf)
        return;

    const int item_size =
        DockLayoutMetrics::item_size_for(
            m_icon_size);

    const int source_width =
        m_icon_pixbuf->get_width();

    const int source_height =
        m_icon_pixbuf->get_height();

    const int source_extent =
        std::max(
            source_width,
            source_height);

    const int zoom_percent =
        std::min(
            DockLayoutMetrics::
                HOVER_ZOOM_PERCENT,
            item_size * 100 /
                std::max(1, source_extent));

    const int target_width =
        std::max(
            source_width,
            source_width * zoom_percent /
                100);

    const int target_height =
        std::max(
            source_height,
            source_height * zoom_percent /
                100);

    m_zoom_frames.reserve(
        ZOOM_FRAME_COUNT);

    for (int frame_index = 0;
         frame_index < ZOOM_FRAME_COUNT;
         ++frame_index)
    {
        const int width =
            source_width +
            (target_width - source_width) *
                frame_index /
                (ZOOM_FRAME_COUNT - 1);

        const int height =
            source_height +
            (target_height - source_height) *
                frame_index /
                (ZOOM_FRAME_COUNT - 1);

        const int icon_x =
            (item_size - width) / 2;

        const int icon_y =
            (item_size - height) / 2;

        auto frame =
            create_transparent_pixbuf(
                item_size,
                item_size);

        m_icon_pixbuf->composite(
            frame,
            icon_x,
            icon_y,
            width,
            height,
            icon_x,
            icon_y,
            static_cast<double>(width) /
                source_width,
            static_cast<double>(height) /
                source_height,
            Gdk::INTERP_BILINEAR,
            255);

        m_zoom_frames.push_back(
            frame);
    }
}

void DockItem::start_zoom_animation()
{
    if (m_zoom_frame ==
            m_zoom_target_frame ||
        m_zoom_animation.connected())
    {
        return;
    }

    m_zoom_animation =
        Glib::signal_timeout().connect(
            sigc::mem_fun(
                *this,
                &DockItem::
                    advance_zoom_animation),
            ZOOM_FRAME_INTERVAL_MS);
}

bool DockItem::advance_zoom_animation()
{
    if (m_hover_effect !=
            DockHoverEffect::zoom ||
        m_zoom_frames.empty())
    {
        return false;
    }

    if (m_zoom_frame <
        m_zoom_target_frame)
    {
        ++m_zoom_frame;
    }
    else if (m_zoom_frame >
             m_zoom_target_frame)
    {
        --m_zoom_frame;
    }

    image.set(
        m_zoom_frames[
            static_cast<std::size_t>(
                m_zoom_frame)]);

    return m_zoom_frame !=
           m_zoom_target_frame;
}

void DockItem::create_blur_frames()
{
    m_blur_animation.disconnect();
    m_blur_frames.clear();
    m_blur_frame = 0;
    m_blur_target_frame = 0;

    if (!m_icon_pixbuf)
        return;

    const int item_size =
        DockLayoutMetrics::item_size_for(
            m_icon_size);

    const int icon_x =
        (item_size -
         m_icon_pixbuf->get_width()) /
        2;

    const int icon_y =
        (item_size -
         m_icon_pixbuf->get_height()) /
        2;

    std::vector<float> icon_alpha(
        static_cast<std::size_t>(
            item_size * item_size),
        0.0F);

    const auto *icon_pixels =
        m_icon_pixbuf->get_pixels();

    const int icon_width =
        m_icon_pixbuf->get_width();

    const int icon_height =
        m_icon_pixbuf->get_height();

    const int icon_rowstride =
        m_icon_pixbuf->get_rowstride();

    const int icon_channels =
        m_icon_pixbuf->get_n_channels();

    const bool icon_has_alpha =
        m_icon_pixbuf->get_has_alpha();

    for (int y = 0; y < icon_height; ++y)
    {
        const auto *row =
            icon_pixels +
            y * icon_rowstride;

        for (int x = 0; x < icon_width; ++x)
        {
            const auto *pixel =
                row + x * icon_channels;

            icon_alpha[
                static_cast<std::size_t>(
                    (icon_y + y) *
                        item_size +
                    icon_x + x)] =
                icon_has_alpha
                    ? pixel[icon_channels - 1]
                    : 255.0F;
        }
    }

    const int outer_radius =
        std::max(
            2,
            item_size / 24);

    const int inner_radius =
        std::max(
            1,
            item_size / 52);

    const auto outer_alpha =
        blur_alpha(
            icon_alpha,
            item_size,
            item_size,
            outer_radius);

    const auto inner_alpha =
        blur_alpha(
            icon_alpha,
            item_size,
            item_size,
            inner_radius);

    const auto outer_blur =
        create_blur_pixbuf(
            outer_alpha,
            item_size,
            BLUR_OUTER_RED,
            BLUR_OUTER_GREEN,
            BLUR_OUTER_BLUE);

    const auto inner_blur =
        create_blur_pixbuf(
            inner_alpha,
            item_size,
            BLUR_INNER_RED,
            BLUR_INNER_GREEN,
            BLUR_INNER_BLUE);

    m_blur_frames.reserve(
        BLUR_FRAME_COUNT);

    for (int frame_index = 0;
         frame_index < BLUR_FRAME_COUNT;
         ++frame_index)
    {
        auto frame =
            create_transparent_pixbuf(
                item_size,
                item_size);

        const int outer_opacity =
            BLUR_OUTER_MAX_ALPHA *
            frame_index /
            (BLUR_FRAME_COUNT - 1);

        const int inner_opacity =
            BLUR_INNER_MAX_ALPHA *
            frame_index /
            (BLUR_FRAME_COUNT - 1);

        if (outer_opacity > 0)
        {
            outer_blur->composite(
                frame,
                0,
                0,
                item_size,
                item_size,
                0,
                0,
                1.0,
                1.0,
                Gdk::INTERP_BILINEAR,
                outer_opacity);

            inner_blur->composite(
                frame,
                0,
                0,
                item_size,
                item_size,
                0,
                0,
                1.0,
                1.0,
                Gdk::INTERP_BILINEAR,
                inner_opacity);
        }

        m_icon_pixbuf->composite(
            frame,
            icon_x,
            icon_y,
            m_icon_pixbuf->get_width(),
            m_icon_pixbuf->get_height(),
            icon_x,
            icon_y,
            1.0,
            1.0,
            Gdk::INTERP_NEAREST,
            255);

        m_blur_frames.push_back(
            frame);
    }
}

void DockItem::start_blur_animation()
{
    if (m_blur_frame ==
            m_blur_target_frame ||
        m_blur_animation.connected())
    {
        return;
    }

    m_blur_animation =
        Glib::signal_timeout().connect(
            sigc::mem_fun(
                *this,
                &DockItem::
                    advance_blur_animation),
            BLUR_FRAME_INTERVAL_MS);
}

bool DockItem::advance_blur_animation()
{
    if (m_hover_effect !=
            DockHoverEffect::blur ||
        m_blur_frames.empty())
    {
        return false;
    }

    if (m_blur_frame <
        m_blur_target_frame)
    {
        ++m_blur_frame;
    }
    else if (m_blur_frame >
             m_blur_target_frame)
    {
        --m_blur_frame;
    }

    image.set(
        m_blur_frames[
            static_cast<std::size_t>(
                m_blur_frame)]);

    return m_blur_frame !=
           m_blur_target_frame;
}

Glib::RefPtr<Gdk::Pixbuf>
DockItem::create_standard_hover_pixbuf(
    const Glib::RefPtr<Gdk::Pixbuf>
        &source) const
{
    if (!source)
        return {};

    auto highlighted =
        source->copy();

    auto *pixels =
        highlighted->get_pixels();

    const int width =
        highlighted->get_width();

    const int height =
        highlighted->get_height();

    const int rowstride =
        highlighted->get_rowstride();

    const int channels =
        highlighted->get_n_channels();

    for (int y = 0;
         y < height;
         ++y)
    {
        auto *row =
            pixels + y * rowstride;

        for (int x = 0;
             x < width;
             ++x)
        {
            auto *pixel =
                row + x * channels;

            for (int channel = 0;
                 channel < 3;
                 ++channel)
            {
                pixel[channel] =
                    static_cast<guchar>(
                        std::min(
                            255,
                            static_cast<int>(
                                pixel[channel]) *
                                    5 /
                                    4 +
                                24));
            }
        }
    }

    return highlighted;
}
