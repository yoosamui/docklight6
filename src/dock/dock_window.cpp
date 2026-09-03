// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window.cpp
//
// Implementation overview:
// Implements DockWindow construction, simple controller forwarding,
// tooltip scheduling, and autohide inhibition.
//
// Cohesive item, surface, and drag-and-drop behavior lives in the companion
// dock_window_*.cpp translation units.
//
// ------------------------------------------------------------

#include "dock_window.h"
#include "dock_session_item.h"
#include "dialogs/dock_session_dialog.h"
#include "presentation/docklight_surface_identity.h"

#include "dock_constants.h"
#include "dock_window_controller.h"

#include <gdk/gdkx.h>

#include <algorithm>
#include <memory>
#include <vector>

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

    m_dock_box.get_style_context()
        ->add_provider(
            m_visual_css,
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION +
                1);

    get_style_context()->add_provider(
        m_visual_css,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION +
            1);

    create_dock(runtime_info);

    m_effective_icon_size =
        std::max(
            1,
            m_controller
                ->settings()
                .icon_size());

    apply_visual_style();
    m_controller->initialize();
}

DockWindow::~DockWindow()
{
    m_dock_item_sync.disconnect();
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

    // On X11, activating a window can change the topmost GdkWindow below
    // the pointer before the pointer itself has moved. Asking the dock's
    // GdkWindow for the device position then returns no pointer window and
    // autohide incorrectly treats the activation as a leave. Use root
    // coordinates for the physical dock rectangle instead; the dock must
    // remain visible until the pointer actually leaves that rectangle.
    if (GDK_IS_X11_DISPLAY(display))
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
