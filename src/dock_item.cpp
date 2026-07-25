#include "dock_item.h"
#include "dock_layout_metrics.h"
#include "dock_window.h"

#include <algorithm>
#include <iostream>

DockItem::DockItem(
    DockWindow &dock,
    Glib::RefPtr<Gio::AppInfo> app,
    int icon_size)
    : m_dock(dock),
      m_app(app)
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

    signal_size_allocate().connect(
        [](Gtk::Allocation &alloc)
        {
            std::cout
                << "DockItem allocation: "
                << alloc.get_width()
                << " x "
                << alloc.get_height()
                << std::endl;
        });
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

        image.set(pixbuf);
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

// void DockWindow::show_tooltip(DockItem &item)
// {
//     std::cout << "DockWindow tooltip: "
//               << item.app_name()
//               << std::endl;

//     m_overlay_widget.show_tooltip(
//         item.app_name(),
//         200,
//         10);
// }

bool DockItem::on_enter_notify_event(
    GdkEventCrossing *)
{
    std::cout << "ENTER: "
              << app_name()
              << std::endl;

    m_dock.schedule_show_tooltip(*this);

    return true;
}

bool DockItem::on_leave_notify_event(
    GdkEventCrossing *)
{
    std::cout << "LEAVE: "
              << app_name()
              << std::endl;

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
        catch (const Glib::Error &e)
        {
            std::cerr
                << e.what()
                << std::endl;
        }
    }

    return true;
}
