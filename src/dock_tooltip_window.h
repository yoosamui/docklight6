#pragma once



#include <gtkmm/window.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/label.h>
#include <gtkmm/eventbox.h>
#include <glibmm/ustring.h>
#include <sigc++/sigc++.h>

#include "dock_enums.h"
#include "dock_layout_engine.h"

class DockTooltipWindow : public Gtk::Window
{
public:
    DockTooltipWindow();

    // The layout engine must receive this width before calculating the
    // position, otherwise a variable-width tooltip would not stay centered.
    int preferred_width_for(const Glib::ustring &text);

    void set_rounded_corners(
        bool enabled,
        int radius,
        int icon_size);

    int tooltip_height() const;
    int tooltip_distance() const;

    void show_tooltip(
        const Glib::ustring &text,
        DockLocation location,
        int tooltip_width,
        const ScreenPosition &position);

    void hide_tooltip();

private:
    void cancel_reveal();
    void make_input_transparent();
    bool is_current_request(
        const Glib::ustring &text,
        DockLocation location,
        int tooltip_width,
        const ScreenPosition &position) const;
    void apply_position(
        DockLocation location,
        const ScreenPosition &position);

private:
    Gtk::Label m_label;
    Gtk::EventBox m_event_box;
    Glib::RefPtr<Gtk::CssProvider> m_visual_css;
    int m_tooltip_height = DockLayoutMetrics::TOOLTIP_HEIGHT;
    int m_tooltip_distance = DockLayoutMetrics::TOOLTIP_DISTANCE;
    sigc::connection m_reveal_timer;
    bool m_has_request = false;
    Glib::ustring m_request_text;
    DockLocation m_request_location = DockLocation::bottom;
    int m_request_width = 0;
    ScreenPosition m_request_position;
};
