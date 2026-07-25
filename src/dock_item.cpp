#include "dock_item.h"
#include "dock_layout_metrics.h"
#include "dock_window.h"

#include <algorithm>

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
        Gdk::LEAVE_NOTIFY_MASK);

    image.set_halign(Gtk::ALIGN_CENTER);
    image.set_valign(Gtk::ALIGN_CENTER);
    add(image);

    set_icon_size(icon_size);

    show_all_children();
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
    m_hover_effect = effect;
    apply_hover_effect();
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
    if (event->button == 1)
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

void DockItem::apply_hover_effect()
{
    if (!m_icon_pixbuf)
        return;

    // The remaining enum values are reserved for later implementations.
    // Until then they deliberately use the safe, layout-neutral effect.
    switch (m_hover_effect)
    {
    case DockHoverEffect::standard:
    case DockHoverEffect::zoom:
    case DockHoverEffect::pixels:
    case DockHoverEffect::glow:
        image.set(
            m_hovered && m_hover_pixbuf
                ? m_hover_pixbuf
                : m_icon_pixbuf);
        break;
    }
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
