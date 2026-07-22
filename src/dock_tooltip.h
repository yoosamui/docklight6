#pragma once

#include <gtkmm.h>
#include "dock_types.h"

class DockTooltip : public Gtk::Window
{
public:
    DockTooltip();

    void set_text(const Glib::ustring &text);

    void show(
        const Glib::ustring &text,
        const TooltipLayout &layout);

  //  void hide_tooltip();

private:
    Gtk::Label m_label;
};