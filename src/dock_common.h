#pragma once
#include <gtkmm.h>

namespace DockCommon
{

    bool get_monitor_geometry(Glib::RefPtr<Gdk::Window> window , int &width, int &height);

}