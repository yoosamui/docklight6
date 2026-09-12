// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window.cpp
//
// Implementation overview:
// Native X11 magnification uses a reusable complete-frame buffer and painted margins.
// Implements DockWindow construction, magnified overflow painting, simple
// controller forwarding, tooltip scheduling, and autohide inhibition.
// Root pointer coordinates are trusted only on native X11, not XWayland.
// The native input shape includes magnification capacity during internal drags.
// Autohide containment follows that same visible input area.
//
// Cohesive item, surface, and drag-and-drop behavior lives in the companion
// dock_window_*.cpp translation units.
//
// ------------------------------------------------------------

#include "dock_window.h"
#include "dock_home_item.h"
#include "dock_session_item.h"
#include "dialogs/dock_session_dialog.h"
#include "presentation/docklight_surface_identity.h"
#include "presentation/presentation_selector.h"

#include "dock_constants.h"
#include "dock_window_controller.h"

#include <gdk/gdkx.h>
#include <gdkmm/general.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

DockMagnifiedLayer::DockMagnifiedLayer()
{
    // Paint into Gtk::Overlay's existing surface. A native child window makes
    // transparent animation frames compositor-dependent and can hide the
    // dock background while the child is repainted.
    set_has_window(false);
    set_app_paintable(true);
    set_double_buffered(true);
    set_events(static_cast<Gdk::EventMask>(0));
    set_can_focus(false);
    set_sensitive(false);
}

void DockMagnifiedLayer::set_icons(
    std::vector<DockMagnifiedIcon> icons)
{
    m_icons = std::move(icons);
}

void DockMagnifiedLayer::paint(
    const Cairo::RefPtr<Cairo::Context> &context)
    const
{
    if (!context)
        return;

    context->save();
    context->set_operator(Cairo::OPERATOR_OVER);

    // Keep indicators in the moved visual frame, but composite them beneath
    // the icons. In particular, a top dock grows downward over its indicator;
    // painting the marker last would draw it across the enlarged icon.
    for (const auto &icon : m_icons)
    {
        DockItem::paint_indicator_visual(
            context,
            icon.indicator,
            icon.indicator_x,
            icon.indicator_y,
            icon.indicator_width,
            icon.indicator_height);
    }

    for (const auto &icon : m_icons)
    {
        if (!icon.pixbuf)
            continue;

        Gdk::Cairo::set_source_pixbuf(
            context,
            icon.pixbuf,
            icon.x,
            icon.y);
        context->paint();
    }

    context->restore();
}

bool DockMagnifiedLayer::on_draw(
    const Cairo::RefPtr<Cairo::Context> &context)
{
    paint(context);
    return true;
}

DockWindow::DockWindow(
    const DockConfiguration &configuration,
    const Glib::RefPtr<Gdk::Monitor>
        &monitor,
    WindowRegistry *window_registry,
    const DockRuntimeInfo &runtime_info)
    : m_window_registry(window_registry)
{
    m_controller =
        std::make_unique<DockWindowController>(
            *this,
            configuration,
            monitor);

    set_decorated(false);
    set_resizable(false);
    set_app_paintable(true);
    set_accept_focus(false);
    set_focus_on_map(false);
    set_title("Docklight 6 Dock");
    add_events(
        Gdk::LEAVE_NOTIFY_MASK |
        Gdk::POINTER_MOTION_MASK);

    // Rounded CSS corners expose pixels from the toplevel underneath the
    // dock box. Give that toplevel an alpha-capable visual so those pixels
    // remain transparent at both ends of the dock.
    auto screen = get_screen();

    if (screen)
    {
        auto rgba_visual =
            screen->get_rgba_visual();

        if (rgba_visual)
        {
            gtk_widget_set_visual(
                GTK_WIDGET(gobj()),
                rgba_visual->gobj());
        }
    }

    // Clear the complete layer surface before GTK paints the rounded dock
    // box. A transparent CSS background does not necessarily replace pixels
    // left behind when a mapped layer surface shrinks, which can make the
    // lower corners look square.
    signal_draw().connect(
        [](const Cairo::RefPtr<Cairo::Context> &context)
        {
            context->save();
            context->set_operator(
                Cairo::OPERATOR_SOURCE);
            context->set_source_rgba(
                0.0,
                0.0,
                0.0,
                0.0);
            context->paint();
            context->restore();

            return false;
        },
        false);

    GtkWindow *gtk_win =
        GTK_WINDOW(gobj());

    gtk_window_set_role(
        gtk_win,
        DocklightSurfaceIdentity::DOCK_ROLE);

    m_surface_backend =
        create_dock_surface_backend(
            *this,
            monitor);

    m_overlay_window.set_monitor(
        monitor);

    get_style_context()->add_class(
        "dock-window");

    const std::vector<Gtk::TargetEntry>
        drag_targets = {
            Gtk::TargetEntry(
                DockConstants::
                    DOCK_ITEM_DRAG_TARGET,
                Gtk::TARGET_SAME_APP)};

    drag_dest_set(
        drag_targets,
        Gtk::DestDefaults(0),
        Gdk::ACTION_MOVE);

    m_dock_box.get_style_context()
        ->add_class("dock-surface");

    m_visual_css =
        Gtk::CssProvider::create();

    m_dock_alignment.set(
        Gtk::ALIGN_FILL,
        Gtk::ALIGN_END,
        1.0F,
        0.0F);
    m_dock_alignment.add(m_dock_box);
    m_dock_alignment.show();
    m_dock_overlay.add(m_dock_alignment);
    add(m_dock_overlay);

    m_dock_box.get_style_context()
        ->add_provider(
            m_visual_css,
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION +
                1);

    get_style_context()->add_provider(
        m_visual_css,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION +
            1);

    // Child allocations can change inside the fixed magnification surface.
    // Apply after allocation, using coordinates relative to the toplevel.
    m_dock_box.signal_size_allocate().connect(
        [this](Gtk::Allocation &)
        {
            update_surface_input_region();
        });
    signal_size_allocate().connect(
        [this](Gtk::Allocation &)
        {
            update_surface_input_region();
        });
    signal_realize().connect(
        sigc::mem_fun(
            *this,
            &DockWindow::update_surface_input_region));
    signal_map().connect(
        sigc::mem_fun(
            *this,
            &DockWindow::update_surface_input_region));

    create_dock(runtime_info);

    m_effective_icon_size =
        std::max(
            1,
            m_controller
                ->settings()
                .icon_size());

    set_magnified_enabled(
        m_controller->settings().hover_effect() ==
        DockHoverEffect::magnified);

    apply_visual_style();
    m_controller->initialize();
}

DockWindow::~DockWindow()
{
    m_dock_item_sync.disconnect();
    if (m_magnified_tick_callback != 0)
    {
        remove_tick_callback(
            m_magnified_tick_callback);
        m_magnified_tick_callback = 0;
    }
}

bool DockWindow::on_draw(
    const Cairo::RefPtr<Cairo::Context> &context)
{
    if (m_magnified_x11_buffered)
    {
        const int scale = std::max(1, get_scale_factor());
        const int width = std::max(1, get_allocated_width()) * scale;
        const int height = std::max(1, get_allocated_height()) * scale;
        if (!m_magnified_x11_frame ||
            m_magnified_x11_frame->get_width() != width ||
            m_magnified_x11_frame->get_height() != height ||
            m_magnified_x11_frame_scale != scale)
        {
            m_magnified_x11_frame = Cairo::ImageSurface::create(
                Cairo::FORMAT_ARGB32, width, height);
            cairo_surface_set_device_scale(
                m_magnified_x11_frame->cobj(), scale, scale);
            m_magnified_x11_frame_scale = scale;
        }
        auto frame = Cairo::Context::create(m_magnified_x11_frame);
        frame->set_operator(Cairo::OPERATOR_CLEAR);
        frame->paint();
        frame->set_operator(Cairo::OPERATOR_OVER);

        int box_x = 0;
        int box_y = 0;
        m_dock_box.translate_coordinates(*this, 0, 0, box_x, box_y);
        const bool horizontal =
            m_dock_box.get_orientation() == Gtk::ORIENTATION_HORIZONTAL;
        const double extra = m_magnified_painted_margin_extra;
        const double x = box_x - (horizontal ? extra : 0.0);
        const double y = box_y - (horizontal ? 0.0 : extra);
        const double body_width = m_dock_box.get_allocated_width() +
            (horizontal ? 2.0 * extra : 0.0);
        const double body_height = m_dock_box.get_allocated_height() +
            (horizontal ? 0.0 : 2.0 * extra);
        auto style = m_dock_box.get_style_context();
        style->render_background(frame, x, y, body_width, body_height);
        style->render_frame(frame, x, y, body_width, body_height);
        Gtk::Window::on_draw(frame);
        m_magnified_layer.paint(frame);

        // As in Plank's renderer, present background and icons as one frame.
        // SOURCE also replaces pixels exposed when the painted body contracts.
        context->save();
        context->set_operator(Cairo::OPERATOR_SOURCE);
        context->set_source(m_magnified_x11_frame, 0.0, 0.0);
        context->paint();
        context->restore();
        return true;
    }

    const bool handled =
        Gtk::Window::on_draw(context);
    if (m_magnified_pointer_active)
        m_magnified_layer.paint(context);
    return handled;
}

bool DockWindow::on_leave_notify_event(
    GdkEventCrossing *event)
{
    // A leave-notify can be generated while the pointer crosses from the
    // toplevel into one of the dock's child widgets. Release only after the
    // pointer has actually left the visible dock body; otherwise the
    // animation reverses during ordinary icon-to-icon motion.
    if (!pointer_is_over_dock_body())
        release_magnified_hover();
    return Gtk::Window::on_leave_notify_event(event);
}

bool DockWindow::on_motion_notify_event(
    GdkEventMotion *event)
{
    if (event &&
        m_magnified_enabled &&
        m_magnified_pointer_active &&
        get_window() &&
        event->window == get_window()->gobj())
    {
        update_magnified_hover(
            static_cast<int>(std::lround(event->x)),
            static_cast<int>(std::lround(event->y)));
    }

    return Gtk::Window::on_motion_notify_event(event);
}

void DockWindow::set_magnified_enabled(bool enabled)
{
    m_magnified_enabled = enabled;
    update_surface_input_region();
    if (m_home_item)
        m_home_item->set_magnified_enabled(enabled);

    set_magnified_layer_active(false);
    if (!enabled)
    {
        m_magnified_layer.set_icons({});
        queue_draw();
    }
}

void DockWindow::apply_configuration(
    const DockConfiguration &configuration)
{
    m_controller->apply_configuration(
        configuration);
}

void DockWindow::set_monitor(
    const Glib::RefPtr<Gdk::Monitor>
        &monitor)
{
    m_controller->set_monitor(monitor);
}

void DockWindow::request_reveal()
{
    m_controller->request_reveal();
}

void DockWindow::schedule_show_tooltip(
    DockItem &item)
{
    if (!item.window_entries().empty())
        m_controller->schedule_show_preview(item);
    else
        m_controller->schedule_show_tooltip(
            item,
            item.tooltip_text());
}

void DockWindow::schedule_show_tooltip(
    Gtk::Widget &item,
    const Glib::ustring &text)
{
    m_controller->schedule_show_tooltip(
        item,
        text);
}

void DockWindow::schedule_hide_tooltip(
    Gtk::Widget &item)
{
    m_controller->schedule_hide_tooltip(item);
}

void DockWindow::hide_tooltip_immediately()
{
    m_controller->hide_tooltip_immediately();
}

void DockWindow::inhibit_autohide()
{
    m_controller->inhibit_autohide();
}

void DockWindow::uninhibit_autohide()
{
    uninhibit_autohide(pointer_is_inside());
}

void DockWindow::uninhibit_autohide(
    bool pointer_inside)
{
    m_controller->uninhibit_autohide(
        pointer_inside);
}

bool DockWindow::set_item_attached(
    DockItem &item,
    bool attached)
{
    if (!m_launcher_manager
             .set_attached(
                 item.desktop_id(),
                 attached))
    {
        return false;
    }

    item.set_attached(attached);
    schedule_dock_item_sync();

    g_message(
        "%s launcher %s",
        attached
            ? "Attached"
            : "Detached",
        item.desktop_id().c_str());

    return true;
}

void DockWindow::edit_session(
    const std::string &session_name)
{
    inhibit_autohide();
    DockSessionDialog::show(
        *this,
        m_home_item->source_icon(),
        m_window_registry,
        m_launcher_manager,
        [this](const SessionRecord &saved)
        {
            for (auto *item : dock_items())
            {
                if (item->desktop_id() ==
                    DockSessionItem::session_desktop_id(saved.name))
                {
                    static_cast<DockSessionItem *>(item)->set_session(saved);
                }
            }
            synchronize_session_items();
        },
        session_name);
    uninhibit_autohide();
}

bool DockWindow::pointer_is_inside()
{
    // Leaving the shaped body emits the last crossing event: transparent
    // overflow cannot emit another leave when the pointer exits the surface.
    // Match the input shape so that overflow cannot cancel autohide forever.
    if (m_magnified_enabled && !m_dragged_item)
        return pointer_is_over_dock_body();

    auto *window = gtk_widget_get_window(
        GTK_WIDGET(gobj()));

    if (!window)
        return false;

    auto *display =
        gdk_window_get_display(window);
    auto *seat = display
                     ? gdk_display_get_default_seat(
                           display)
                     : nullptr;
    auto *pointer = seat
                        ? gdk_seat_get_pointer(seat)
                        : nullptr;

    if (!pointer)
        return false;

    // On native X11, activating a window can change the topmost GdkWindow below
    // the pointer before the pointer itself has moved. Asking the dock's
    // GdkWindow for the device position then returns no pointer window and
    // autohide incorrectly treats the activation as a leave. Use root
    // coordinates for the physical dock rectangle instead; the dock must
    // remain visible until the pointer actually leaves that rectangle.
    if (is_native_x11_presentation())
    {
        int pointer_x = 0;
        int pointer_y = 0;
        int window_x = 0;
        int window_y = 0;

        gdk_device_get_position(
            pointer,
            nullptr,
            &pointer_x,
            &pointer_y);
        get_position(window_x, window_y);

        return pointer_x >= window_x &&
               pointer_y >= window_y &&
               pointer_x <
                   window_x +
                       get_allocated_width() &&
               pointer_y <
                   window_y +
                       get_allocated_height();
    }

    // XWayland freezes root coordinates when the pointer leaves X surfaces.
    // Require a pointer window so that the last dock position cannot veto
    // hiding indefinitely after crossing to a native Wayland application.
    int x = 0;
    int y = 0;
    GdkModifierType modifiers{};

    const auto *pointer_window =
        gdk_window_get_device_position(
            window,
            pointer,
            &x,
            &y,
            &modifiers);

    return pointer_window &&
           x >= 0 &&
           y >= 0 &&
           x < get_allocated_width() &&
           y < get_allocated_height();
}

void DockWindow::set_surface_input_passthrough(
    bool passthrough)
{
    m_surface_input_passthrough = passthrough;
    update_surface_input_region();
}

void DockWindow::update_surface_input_region()
{
    auto window = get_window();
    if (!window)
        return;

    Cairo::RefPtr<Cairo::Region> region;
    if (m_surface_input_passthrough ||
        (m_magnified_enabled && !m_dragged_item))
    {
        region = Cairo::Region::create();
        int x = 0;
        int y = 0;
        if (!m_surface_input_passthrough &&
            m_dock_box.translate_coordinates(*this, 0, 0, x, y))
        {
            // Normal hover uses the body. An internal magnified drag uses
            // the full surface, including the enlarged artwork and gaps.
            const auto allocation = m_dock_box.get_allocation();
            const Cairo::RectangleInt body{
                x,
                y,
                allocation.get_width(),
                allocation.get_height()};
            region->do_union(body);
        }
    }

    window->set_pass_through(m_surface_input_passthrough);
    window->input_shape_combine_region(
        region,
        0,
        0);
}

bool DockWindow::point_is_over_dock_body(int x, int y)
{
    int box_x = 0;
    int box_y = 0;
    if (!m_dock_box.translate_coordinates(
            *this,
            0,
            0,
            box_x,
            box_y))
    {
        return false;
    }

    const auto allocation =
        m_dock_box.get_allocation();
    return x >= box_x &&
           y >= box_y &&
           x < box_x + allocation.get_width() &&
           y < box_y + allocation.get_height();
}

bool DockWindow::pointer_is_over_dock_body()
{
    auto *window = gtk_widget_get_window(
        GTK_WIDGET(gobj()));
    if (!window)
        return false;

    auto *display =
        gdk_window_get_display(window);
    auto *seat = display
                     ? gdk_display_get_default_seat(
                           display)
                     : nullptr;
    auto *pointer = seat
                        ? gdk_seat_get_pointer(seat)
                        : nullptr;
    if (!pointer)
        return false;

    int x = 0;
    int y = 0;
    if (is_native_x11_presentation())
    {
        int window_x = 0;
        int window_y = 0;
        gdk_device_get_position(
            pointer,
            nullptr,
            &x,
            &y);
        get_position(window_x, window_y);
        x -= window_x;
        y -= window_y;
    }
    else
    {
        GdkModifierType modifiers{};
        if (!gdk_window_get_device_position(
                window,
                pointer,
                &x,
                &y,
                &modifiers))
        {
            return false;
        }
    }

    return point_is_over_dock_body(x, y);
}

DockLocation DockWindow::location() const
{
    return m_controller->location();
}

LauncherManager &DockWindow::launcher_manager()
{
    return m_launcher_manager;
}

bool DockWindow::preview_input_forwarding() const
{
    return m_controller->preview_input_forwarding();
}

void DockWindow::open_settings()
{
    m_home_item->schedule_open_settings();
}

void DockWindow::open_session()
{
    m_home_item->open_session();
}

void DockWindow::show_about()
{
    m_home_item->show_about();
}

void DockWindow::exit_docklight()
{
    m_home_item->exit_docklight();
}
