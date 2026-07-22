#include "dock_item.h"
#include "dock_layout_metrics.h"
#include "dock_settings.h"
#include "dock_window.h"

#include <iostream>

DockItem::DockItem(
    DockWindow &dock,
    Glib::RefPtr<Gio::AppInfo> app)
    : m_dock(dock),
      m_app(app)
{
    set_visible_window(false);

    const int icon_size =
        g_settings.icon_size();

    add_events(
        Gdk::ENTER_NOTIFY_MASK |
        Gdk::LEAVE_NOTIFY_MASK);

    // -------------------------------------------------------
    // Load application icon
    // -------------------------------------------------------

    auto icon = app->get_icon();

    if (icon)
    {
        auto themed =
            Glib::RefPtr<Gio::ThemedIcon>::cast_dynamic(icon);

        if (themed)
        {
            auto names = themed->get_names();

            std::cout << app->get_name() << " -> ";

            for (const auto &name : names)
                std::cout << name << " ";

            std::cout << std::endl;

            if (!names.empty())
            {
                auto icon_name = *names.begin();

                try
                {
                    auto pixbuf =
                        Gtk::IconTheme::get_default()->load_icon(
                            icon_name,
                            icon_size,
                            Gtk::ICON_LOOKUP_USE_BUILTIN);

                    image.set(pixbuf);
                }
                catch (const Glib::Error &e)
                {
                    std::cerr
                        << "Cannot load icon "
                        << icon_name
                        << ": "
                        << e.what()
                        << std::endl;
                }
            }
        }
    }

    // -------------------------------------------------------
    // Layout
    // -------------------------------------------------------

    set_size_request(
        DockLayoutMetrics::item_size_for(icon_size),
        DockLayoutMetrics::item_size_for(icon_size));

    // Slot size derives from the icon-size setting, leaving a consistent
    // padding reserve for indicators and future hover animation.
    image.set_halign(Gtk::ALIGN_CENTER);
    image.set_valign(Gtk::ALIGN_CENTER);
    add(image);

    show_all_children();

    // set_tooltip_text(app_name());

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
