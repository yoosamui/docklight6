// ------------------------------------------------------------
// Docklight 6.0
//
// Implements the transparent layer-shell edge reveal surface.
// ------------------------------------------------------------

#include "dock_reveal_window.h"
#include "dock/dock_constants.h"

#include <gtk-layer-shell.h>

DockRevealWindow::DockRevealWindow()
{
    set_decorated(false);
    set_resizable(false);
    set_app_paintable(true);
    set_accept_focus(false);
    set_focus_on_map(false);
    set_skip_taskbar_hint(true);
    set_skip_pager_hint(true);

    add_events(Gdk::ENTER_NOTIFY_MASK);

    signal_draw().connect(
        [](const Cairo::RefPtr<Cairo::Context> &context)
        {
            context->save();
            context->set_operator(Cairo::OPERATOR_SOURCE);
            context->set_source_rgba(0.0, 0.0, 0.0, 0.0);
            context->paint();
            context->restore();
            return true;
        });

    auto *window = GTK_WINDOW(gobj());

    gtk_layer_init_for_window(window);
    gtk_layer_set_namespace(
        window,
        "docklight6-autohide-reveal");
    gtk_layer_set_layer(
        window,
        GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_keyboard_mode(
        window,
        GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_exclusive_zone(window, 0);
}

void DockRevealWindow::set_monitor(
    const Glib::RefPtr<Gdk::Monitor> &monitor)
{
    prepare_reconfiguration();

    gtk_layer_set_monitor(
        GTK_WINDOW(gobj()),
        monitor ? monitor->gobj() : nullptr);
}

void DockRevealWindow::apply_placement(
    const DockPlacement &placement)
{
    prepare_reconfiguration();

    auto *window = GTK_WINDOW(gobj());

    gtk_layer_set_anchor(
        window,
        GTK_LAYER_SHELL_EDGE_LEFT,
        placement.anchor_left);
    gtk_layer_set_anchor(
        window,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        placement.anchor_right);
    gtk_layer_set_anchor(
        window,
        GTK_LAYER_SHELL_EDGE_TOP,
        placement.anchor_top);
    gtk_layer_set_anchor(
        window,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        placement.anchor_bottom);

    const bool horizontal = placement.is_horizontal();

    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_LEFT,
        horizontal ? placement.margin_left : 0);
    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        horizontal ? placement.margin_right : 0);
    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_TOP,
        horizontal ? 0 : placement.margin_top);
    gtk_layer_set_margin(
        window,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        horizontal ? 0 : placement.margin_bottom);

    gtk_widget_set_size_request(
        GTK_WIDGET(window),
        horizontal
            ? placement.width
            : DockConstants::AUTOHIDE_REVEAL_SIZE,
        horizontal
            ? DockConstants::AUTOHIDE_REVEAL_SIZE
            : placement.height);

    gtk_window_resize(window, 1, 1);
}

void DockRevealWindow::prepare_reconfiguration()
{
    // Layer-shell anchors and monitor selection belong to the underlying
    // Wayland surface. Recreate that surface before changing them; immediately
    // remapping the same surface can cause KWin to terminate the client with a
    // protocol error, while changing it in place can retain the old input edge.
    if (get_mapped())
        hide();

    if (get_realized())
        gtk_widget_unrealize(GTK_WIDGET(gobj()));
}

sigc::signal<void> &
DockRevealWindow::signal_reveal_requested()
{
    return m_signal_reveal_requested;
}

bool DockRevealWindow::on_enter_notify_event(
    GdkEventCrossing *event)
{
    if (!event ||
        event->detail != GDK_NOTIFY_INFERIOR)
    {
        m_signal_reveal_requested.emit();
    }

    return true;
}
