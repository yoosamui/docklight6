#pragma once

#include "dock_layout_metrics.h"
#include "dock_layout_types.h"

#include <gdkmm/monitor.h>
#include <glibmm/ustring.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/eventbox.h>
#include <gtkmm/label.h>
#include <gtkmm/window.h>
#include <sigc++/sigc++.h>

class DockTooltipWindow : public Gtk::Window
{
public:
    DockTooltipWindow();

    void set_monitor(
        const Glib::RefPtr<Gdk::Monitor>
            &monitor);

    void set_rounded_corners(
        bool enabled,
        int radius,
        int icon_size);
    void show_tooltip(
        const Glib::ustring &text,
        DockLocation location,
        int tooltip_width,
        const ScreenPosition &position);
    void hide_tooltip();

    // The layout engine must receive this width before calculating the
    // position, otherwise a variable-width tooltip would not stay centered.
    int preferred_width_for(const Glib::ustring &text);
    int tooltip_height() const;
    int tooltip_distance() const;

private:
    void cancel_reveal();
    void make_input_transparent();
    void apply_position(
        DockLocation location,
        const ScreenPosition &position);

    bool is_current_request(
        const Glib::ustring &text,
        DockLocation location,
        int tooltip_width,
        const ScreenPosition &position) const;

private:
    Glib::RefPtr<Gtk::CssProvider> m_visual_css;
    Glib::ustring m_request_text;

    Gtk::Label m_label;
    Gtk::EventBox m_event_box;

    sigc::connection m_reveal_timer;

    DockLocation m_request_location =
        DockLocation::bottom;

    ScreenPosition m_request_position;

    int m_tooltip_height = DockLayoutMetrics::TOOLTIP_HEIGHT;
    int m_tooltip_distance = DockLayoutMetrics::TOOLTIP_DISTANCE;
    int m_icon_size = DockLayoutMetrics::BASE_ICON_SIZE;
    int m_request_width = 0;

    bool m_has_request = false;
};
