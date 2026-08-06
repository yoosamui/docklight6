// ------------------------------------------------------------
// Docklight 6.0
//
// Declares the transparent edge surface used to reveal a hidden dock.
// ------------------------------------------------------------

#pragma once

#include "layout/dock_layout_types.h"

#include <gdkmm/monitor.h>
#include <gtkmm/window.h>
#include <sigc++/signal.h>

class DockRevealWindow : public Gtk::Window
{
public:
    DockRevealWindow();

    void set_monitor(
        const Glib::RefPtr<Gdk::Monitor> &monitor);
    void apply_placement(
        const DockPlacement &placement);

    sigc::signal<void> &signal_reveal_requested();

private:
    void prepare_reconfiguration();
    bool on_enter_notify_event(
        GdkEventCrossing *event) override;

private:
    sigc::signal<void> m_signal_reveal_requested;
};
