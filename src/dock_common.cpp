#include "dock_common.h"

namespace DockCommon
{

    bool get_monitor_geometry(Glib::RefPtr<Gdk::Window> window, Gdk::Rectangle &geometry)
    {
        if (!window)
            return false;

        auto display = window->get_display(); // Faster than get_default()
        auto monitor = display->get_monitor_at_window(window);

        if (!monitor)
            return false;

        monitor->get_geometry(geometry);

        return true;
    }

    bool get_monitor_geometry(Glib::RefPtr<Gdk::Window> window, int &width, int &height)
    {
        Gdk::Rectangle geometry;

        // Pass directly to the core function
        if (!get_monitor_geometry(window, geometry))
            return false;

        width = geometry.get_width();
        height = geometry.get_height();

        return true;
    }

    /*
    bool get_monitor_geometry(Glib::RefPtr<Gdk::Window> window, Gdk::Rectangle &geometry)
    {

        if (!window)
            return false;


        auto display = Gdk::Display::get_default();
        if (!display)
            return false;

        auto monitor = display->get_monitor_at_window(window);

        if (!monitor)
            return false;

        monitor->get_geometry(geometry);
        return true;
    }

    bool get_monitor_geometry(Glib::RefPtr<Gdk::Window> window, int &width, int &height)
    {

        if (!window)
            return false;

        Gdk::Rectangle geometry;
        if (!get_monitor_geometry(window, geometry))
            return false;

        width = geometry.get_width();
        height = geometry.get_height();

        return true;
    }
        */

}