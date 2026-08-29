// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// tooltip_manager.h
//
// Purpose:
// Declares tooltip hover intent, timing, placement, and visibility control.
//
// Responsibilities:
// - Track pending, hovered, and visible dock items.
// - Schedule tooltip show and hide transitions.
// - Calculate tooltip placement from dock and monitor geometry.
// - Signal cross-surface policy events to DockWindowController.
//
// Dependencies and ownership:
// TooltipManager borrows DockWindow and item widgets, owns timer connections,
// and delegates the actual tooltip surface to DockWindow's overlay window.
//
// Design notes:
// Preview coordination stays in DockWindowController rather than being
// coupled directly into this focused manager.
//
// ------------------------------------------------------------

#pragma once

#include "config/dock_configuration.h"
#include "layout/dock_layout_engine.h"
#include "layout/dock_layout_geometry.h"

#include <gdkmm/monitor.h>
#include <glibmm/ustring.h>
#include <sigc++/connection.h>
#include <sigc++/signal.h>

#include <functional>

class DockWindow;

namespace Gtk
{
class Widget;
}

class TooltipManager
{
public:
    TooltipManager(
        DockWindow &window,
        std::function<ScreenPosition()> dock_position);
    ~TooltipManager();

    void schedule_show(
        Gtk::Widget &item,
        const Glib::ustring &text,
        bool preserve_pending_preview = false);
    void show_immediately(
        Gtk::Widget &item,
        const Glib::ustring &text);
    void schedule_hide(Gtk::Widget &item);
    void hide_immediately();
    void hide();

    void apply_configuration(const DockSettings &settings);
    void set_layout_request(const DockLayoutRequest &request);
    void set_monitor(const Glib::RefPtr<Gdk::Monitor> &monitor);
    void set_layout_geometry(
        const MonitorGeometry &usable_monitor,
        const MonitorGeometry &output);

    void begin_item_hover(Gtk::Widget &item);
    void cancel_show_timer();
    void cancel_hide_timer();
    void start_hide_timer(std::function<bool()> pointer_inside);

    bool has_request_for(const Gtk::Widget &item) const;
    bool pointer_inside() const;
    Gtk::Widget *hovered_item() const;

    sigc::signal<void, bool> &signal_will_show();
    sigc::signal<void> &signal_hide_requested();

private:
    void show_now(
        Gtk::Widget &item,
        const Glib::ustring &text,
        bool preserve_pending_preview);
    void start_show_timer(
        Gtk::Widget &item,
        const Glib::ustring &text,
        bool preserve_pending_preview);

private:
    DockWindow &m_window;
    DockLayoutEngine m_layout_engine;
    DockLayoutGeometry m_layout_geometry;
    std::function<ScreenPosition()> m_dock_position;

    Gtk::Widget *m_pending_item = nullptr;
    Gtk::Widget *m_hovered_item = nullptr;
    Gtk::Widget *m_visible_item = nullptr;
    sigc::connection m_show_timer;
    sigc::connection m_hide_timer;
    unsigned long long m_request_generation = 0;
    bool m_pointer_inside = false;
    DockSettings m_settings;
    DockLayoutRequest m_layout_request;
    MonitorGeometry m_usable_monitor_geometry;
    MonitorGeometry m_output_geometry;
    Glib::RefPtr<Gdk::Monitor> m_monitor;

    sigc::signal<void, bool> m_signal_will_show;
    sigc::signal<void> m_signal_hide_requested;
};
