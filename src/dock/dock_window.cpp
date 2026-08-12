// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window.cpp
//
// Implementation overview:
// Implements the main dock surface, item synchronization, launcher
// ordering, drag-and-drop, and application of calculated placement.
//
// Important implementation decisions:
// - Dock item identity is based on normalized desktop identifiers.
// - The controller calculates placement; this file performs GTK effects.
// - Item synchronization preserves configured order while merging apps.
// - Drag reorder writes through LauncherManager before rebuilding widgets.
// - Visible spacer widgets express main-axis content margins.
//
// ------------------------------------------------------------

#include "dock_window.h"
#include "dock_home_item.h"

#include "dock_constants.h"
#include "layout/dock_layout_metrics.h"
#include "dock_window_controller.h"
#include "launchers/launcher_manager.h"
#include "windowing/running_application.h"
#include "windowing/window_registry.h"

#include <gtk-layer-shell.h>
#include <gdk/gdkwayland.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{

bool environment_contains(
    const char *name,
    const std::string &needle)
{
    const auto value =
        std::getenv(name);

    if (!value)
        return false;

    std::string normalized(value);
    std::transform(
        normalized.begin(),
        normalized.end(),
        normalized.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(
                std::tolower(character));
        });

    return normalized.find(needle) !=
           std::string::npos;
}

bool is_gnome_wayland_session()
{
    return environment_contains(
               "XDG_SESSION_TYPE",
               "wayland") &&
           (environment_contains(
                "XDG_CURRENT_DESKTOP",
                "gnome") ||
            environment_contains(
                "XDG_SESSION_DESKTOP",
                "gnome"));
}

}

DockWindow::DockWindow(
    const DockConfiguration &configuration,
    const Glib::RefPtr<Gdk::Monitor>
        &monitor,
    WindowRegistry *window_registry)
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
        "docklight6-dock");

    m_uses_layer_shell =
        gtk_layer_is_supported();

    if (m_uses_layer_shell)
    {
        gtk_layer_init_for_window(gtk_win);
        gtk_layer_set_keyboard_mode(
            gtk_win,
            GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);

        gtk_layer_set_monitor(
            gtk_win,
            monitor
                ? monitor->gobj()
                : nullptr);

        gtk_layer_set_namespace(
            gtk_win,
            "docklight6");

        gtk_layer_set_layer(
            gtk_win,
            GTK_LAYER_SHELL_LAYER_TOP);
    }
    else
    {
        // On X11, Muffin and other EWMH window managers place an ordinary
        // undecorated window like an application (often at screen centre).
        // Mark it as a dock before realization so explicit edge coordinates
        // and struts are honored from the very first map.
        set_type_hint(Gdk::WINDOW_TYPE_HINT_DOCK);
        set_skip_taskbar_hint(true);
        set_skip_pager_hint(true);
        set_keep_above(true);
        stick();
        set_position(Gtk::WIN_POS_NONE);
    }

    // Mutter may paint an ordinary Wayland toplevel once at its provisional
    // centred position before the Shell integration can move its actor. The
    // integration publishes geometry only after placement is committed; the
    // controller restores opacity when that notification arrives.
    auto display = get_display();
    const bool ordinary_gnome_wayland_window =
        is_gnome_wayland_session() &&
        !m_uses_layer_shell &&
        display &&
        GDK_IS_WAYLAND_DISPLAY(display->gobj());

    if (ordinary_gnome_wayland_window)
    {
        m_initial_gnome_placement_pending = true;
        set_opacity(0.0);
    }

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

    create_dock();

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
    if (item.running() &&
        m_controller
            ->settings()
            .display_preview())
    {
        m_controller->schedule_show_preview(item);
    }
    else
    {
        m_controller->schedule_show_tooltip(
            item,
            item.tooltip_text());
    }
}

void DockWindow::schedule_show_tooltip(
    Gtk::Widget &item,
    const Glib::ustring &text)
{
    m_controller->schedule_show_tooltip(
        item,
        text);
}

void DockWindow::schedule_hide_tooltip()
{
    m_controller->schedule_hide_tooltip();
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
    m_controller->uninhibit_autohide(
        pointer_is_inside());
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

void DockWindow::begin_item_drag(
    DockItem &item)
{
    if (!m_dragged_item)
        inhibit_autohide();

    m_dragged_item = &item;
    m_item_drop_accepted = false;
    hide_tooltip_immediately();
}

bool DockWindow::can_drop_item(
    const DockItem &target)
{
    if (!m_dragged_item)
        return false;

    const auto items = dock_items();

    return std::find(
               items.begin(),
               items.end(),
               m_dragged_item) !=
               items.end() &&
           std::find(
               items.begin(),
               items.end(),
               &target) !=
               items.end();
}

bool DockWindow::drop_item(
    DockItem &target,
    int x,
    int y)
{
    if (!can_drop_item(target))
        return false;

    auto items = dock_items();

    if (!m_dragged_item->attached() &&
        !set_item_attached(
            *m_dragged_item,
            true))
    {
        return false;
    }

    if (m_dragged_item == &target)
    {
        m_item_drop_accepted =
            apply_dragged_item_order(
                items);
        return m_item_drop_accepted;
    }

    const bool horizontal =
        m_controller
                ->layout_request()
                .location ==
            DockLocation::bottom ||
        m_controller
                ->layout_request()
                .location ==
            DockLocation::top;

    const auto allocation =
        target.get_allocation();

    const bool insert_after =
        horizontal
            ? x >= allocation.get_width() / 2
            : y >= allocation.get_height() / 2;

    items.erase(
        std::remove(
            items.begin(),
            items.end(),
            m_dragged_item),
        items.end());

    auto insertion =
        std::find(
            items.begin(),
            items.end(),
            &target);

    if (insertion == items.end())
        return false;

    if (insert_after)
        ++insertion;

    items.insert(
        insertion,
        m_dragged_item);

    m_item_drop_accepted =
        apply_dragged_item_order(
            items);
    return m_item_drop_accepted;
}

void DockWindow::end_item_drag(
    DockItem &item)
{
    if (m_dragged_item == &item)
    {
        const bool pointer_inside =
            m_item_drop_accepted ||
            pointer_is_inside();

        m_dragged_item = nullptr;
        m_controller->finish_autohide_drag(
            pointer_inside);
        m_item_drop_accepted = false;
    }
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

bool DockWindow::on_drag_motion(
    const Glib::RefPtr<
        Gdk::DragContext> &context,
    int x,
    int y,
    guint time)
{
    if (!is_first_item_drop_zone(
            x,
            y))
    {
        return false;
    }

    context->drag_status(
        Gdk::ACTION_MOVE,
        time);
    return true;
}

bool DockWindow::on_drag_drop(
    const Glib::RefPtr<
        Gdk::DragContext> &context,
    int x,
    int y,
    guint time)
{
    const bool accepted =
        is_first_item_drop_zone(
            x,
            y) &&
        drop_item_first();

    context->drag_finish(
        accepted,
        false,
        time);

    return accepted;
}

bool DockWindow::is_first_item_drop_zone(
    int x,
    int y)
{
    if (!m_dragged_item)
        return false;

    const auto items = dock_items();

    if (items.empty())
        return false;

    int first_x = 0;
    int first_y = 0;

    if (!items.front()
             ->translate_coordinates(
                 *this,
                 0,
                 0,
                 first_x,
                 first_y))
    {
        return false;
    }

    const auto allocation =
        items.front()->get_allocation();

    const bool horizontal =
        m_controller
                ->layout_request()
                .location ==
            DockLocation::bottom ||
        m_controller
                ->layout_request()
                .location ==
            DockLocation::top;

    return horizontal
               ? x <=
                     first_x +
                         allocation
                                 .get_width() /
                             2
               : y <=
                     first_y +
                         allocation
                                 .get_height() /
                             2;
}

bool DockWindow::drop_item_first()
{
    if (!m_dragged_item)
        return false;

    auto items = dock_items();

    if (std::find(
            items.begin(),
            items.end(),
            m_dragged_item) ==
        items.end())
    {
        return false;
    }

    if (!m_dragged_item->attached() &&
        !set_item_attached(
            *m_dragged_item,
            true))
    {
        return false;
    }

    items.erase(
        std::remove(
            items.begin(),
            items.end(),
            m_dragged_item),
        items.end());

    items.insert(
        items.begin(),
        m_dragged_item);

    m_item_drop_accepted =
        apply_dragged_item_order(
            items);
    return m_item_drop_accepted;
}

bool DockWindow::apply_dragged_item_order(
    const std::vector<DockItem *>
        &items)
{
    int position = 2;

    for (auto *item : items)
    {
        m_dock_box.reorder_child(
            *item,
            position++);
    }

    std::vector<std::string>
        attached_ids;

    for (const auto *item : items)
    {
        if (item->attached())
        {
            attached_ids.push_back(
                item->desktop_id());
        }
    }

    if (!m_launcher_manager
             .reorder_attached(
                 attached_ids))
    {
        g_warning(
            "Cannot persist reordered dock items");
    }

    m_controller->dock_items_reordered();
    return true;
}

DockLocation DockWindow::location() const
{
    return m_controller->location();
}

DockWindowGeometry
DockWindow::content_geometry() const
{
    DockWindowGeometry geometry;

    const bool horizontal =
        m_dock_box.get_orientation() ==
        Gtk::ORIENTATION_HORIZONTAL;

    for (auto *child :
         m_dock_box.get_children())
    {
        if (!child->get_visible())
            continue;

        int minimum_width = 0;
        int natural_width = 0;
        int minimum_height = 0;
        int natural_height = 0;

        child->get_preferred_width(
            minimum_width,
            natural_width);

        child->get_preferred_height(
            minimum_height,
            natural_height);

        if (horizontal)
        {
            geometry.width += natural_width;
            geometry.height =
                std::max(
                    geometry.height,
                    natural_height);
        }
        else
        {
            geometry.width =
                std::max(
                    geometry.width,
                    natural_width);
            geometry.height += natural_height;
        }
    }

    return geometry;
}

// Applies a previously calculated placement to the GTK layer-shell surface.
// This function performs compositor-facing side effects but does not derive
// geometry or monitor policy.
void DockWindow::apply_dock_layout(
    const DockPlacement &placement,
    const MonitorGeometry &output,
    const MonitorGeometry &workarea)
{
    apply_visual_style();
    apply_dock_orientation(
        placement.orientation);

    GtkWindow *gtk_win =
        GTK_WINDOW(gobj());

    if (!m_uses_layer_shell)
    {
        // Capture the work area before this window publishes its own strut.
        // This matters for compositor-owned panels (notably Cinnamon's),
        // which reserve space in _NET_WORKAREA without owning an X11 client
        // window whose _NET_WM_STRUT_PARTIAL we could inspect. Reading the
        // work area again after our strut is installed would only see the
        // larger of the panel and DockLight reservations.
        capture_x11_base_workarea(
            output,
            workarea);

        const auto &base_workarea =
            m_has_x11_base_workarea
                ? m_x11_base_workarea
                : workarea;

        // Mutter does not expose layer-shell. Keep every dock edge in the
        // physical output coordinate space and let the GNOME integration
        // manage the ordinary toplevel there. Other window managers retain
        // their work-area-aware placement around compositor panels.
        const auto &edge_area =
            is_gnome_wayland_session()
                ? output
                : base_workarea;

        gtk_widget_set_size_request(
            GTK_WIDGET(gtk_win),
            placement.width,
            placement.height);

        const int width =
            placement.width > 0
                ? placement.width
                : std::max(
                      1,
                      output.width -
                          placement.margin_left -
                          placement.margin_right);
        const int height =
            placement.height > 0
                ? placement.height
                : std::max(
                      1,
                      output.height -
                          placement.margin_top -
                          placement.margin_bottom);
        int x = output.x + (output.width - width) / 2;
        int y = output.y + (output.height - height) / 2;

        if (placement.is_vertical() &&
            placement.anchor_left)
        {
            x = edge_area.x + placement.margin_left;
        }
        else if (placement.is_vertical() &&
                 placement.anchor_right)
        {
            x = edge_area.x + edge_area.width -
                placement.margin_right - width;
        }
        else if (placement.anchor_left)
            x = output.x + placement.margin_left;
        else if (placement.anchor_right)
            x = output.x + output.width - placement.margin_right - width;

        if (placement.is_horizontal() &&
            placement.anchor_top)
        {
            y = edge_area.y + placement.margin_top;
        }
        else if (placement.is_horizontal() &&
                 placement.anchor_bottom)
        {
            y = edge_area.y + edge_area.height -
                placement.margin_bottom - height;
        }
        else if (placement.anchor_top)
            y = output.y + placement.margin_top;
        else if (placement.anchor_bottom)
            y = output.y + output.height - placement.margin_bottom - height;

        resize(width, height);
        move(x, y);
        apply_x11_strut(placement, x, y, width, height);
        return;
    }

    bool anchor_left =
        placement.anchor_left;
    bool anchor_right =
        placement.anchor_right;
    bool anchor_top =
        placement.anchor_top;
    bool anchor_bottom =
        placement.anchor_bottom;

    int margin_left =
        placement.margin_left;
    int margin_right =
        placement.margin_right;
    int margin_top =
        placement.margin_top;
    int margin_bottom =
        placement.margin_bottom;

    // Mutter stretches a layer surface between opposite anchors even when
    // GTK supplies an explicit main-axis size. For centered, non-fill docks,
    // leave that axis unanchored; the layer-shell protocol then centers the
    // compact surface. KWin retains its dual-anchor/equal-margin workaround.
    if (is_gnome_wayland_session())
    {
        if (placement.is_horizontal() &&
            placement.width > 0 &&
            anchor_left &&
            anchor_right &&
            margin_left == margin_right)
        {
            anchor_left = false;
            anchor_right = false;
            margin_left = 0;
            margin_right = 0;
        }
        else if (placement.is_vertical() &&
                 placement.height > 0 &&
                 anchor_top &&
                 anchor_bottom &&
                 margin_top == margin_bottom)
        {
            anchor_top = false;
            anchor_bottom = false;
            margin_top = 0;
            margin_bottom = 0;
        }
    }

    gtk_layer_set_anchor(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_LEFT,
        anchor_left);

    gtk_layer_set_anchor(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        anchor_right);

    gtk_layer_set_anchor(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_TOP,
        anchor_top);

    gtk_layer_set_anchor(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        anchor_bottom);

    gtk_layer_set_margin(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_LEFT,
        margin_left);

    gtk_layer_set_margin(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        margin_right);

    gtk_layer_set_margin(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_TOP,
        margin_top);

    gtk_layer_set_margin(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        margin_bottom);

    // gtk-layer-shell uses the GTK widget request as the surface's natural
    // size. set_default_size() alone does not reliably resize an already
    // mapped layer surface, particularly on the vertical main axis.
    gtk_widget_set_size_request(
        GTK_WIDGET(gtk_win),
        placement.width,
        placement.height);

    // Request a fresh configure after changing the size request. The actual
    // size remains compositor-controlled when opposite anchors are active.
    gtk_window_resize(gtk_win, 1, 1);

    if (placement.exclusive_zone < 0)
    {
        gtk_layer_auto_exclusive_zone_enable(
            gtk_win);
    }
    else
    {
        gtk_layer_set_exclusive_zone(
            gtk_win,
            placement.exclusive_zone);
    }
}

void DockWindow::capture_x11_base_workarea(
    const MonitorGeometry &output,
    const MonitorGeometry &fallback)
{
    if (m_has_x11_base_workarea)
        return;

    auto display = get_display();
    if (!display ||
        !GDK_IS_X11_DISPLAY(display->gobj()))
    {
        return;
    }

    Display *xdisplay =
        gdk_x11_display_get_xdisplay(display->gobj());
    const ::Window root =
        DefaultRootWindow(xdisplay);

    auto read_cardinals =
        [xdisplay, root](
            const char *property_name,
            unsigned long requested,
            std::vector<unsigned long> &values)
        {
            const Atom property = XInternAtom(
                xdisplay,
                property_name,
                False);
            Atom actual_type = None;
            int actual_format = 0;
            unsigned long item_count = 0;
            unsigned long bytes_after = 0;
            unsigned char *data = nullptr;

            const int status = XGetWindowProperty(
                xdisplay,
                root,
                property,
                0,
                static_cast<long>(requested),
                False,
                XA_CARDINAL,
                &actual_type,
                &actual_format,
                &item_count,
                &bytes_after,
                &data);

            if (status != Success ||
                actual_type != XA_CARDINAL ||
                actual_format != 32 ||
                !data)
            {
                if (data)
                    XFree(data);
                return false;
            }

            const auto *cardinals =
                reinterpret_cast<unsigned long *>(data);
            values.assign(
                cardinals,
                cardinals + item_count);
            XFree(data);
            return true;
        };

    std::vector<unsigned long> desktops;
    std::vector<unsigned long> workareas;
    unsigned long desktop = 0;

    if (read_cardinals(
            "_NET_CURRENT_DESKTOP",
            1,
            desktops) &&
        !desktops.empty())
    {
        desktop = desktops.front();
    }

    // A just-terminated DockLight instance can leave Muffin's root work area
    // temporarily reflecting its old strut. Sample before this window maps
    // and keep the least-reserved result, giving the WM time to process the
    // previous client's destruction without accumulating one dock height on
    // every development restart.
    std::vector<unsigned long> best_workareas;
    unsigned long long best_area = 0;

    for (int sample = 0; sample < 20; ++sample)
    {
        if (read_cardinals(
                "_NET_WORKAREA",
                4096,
                workareas) &&
            workareas.size() >= (desktop + 1) * 4)
        {
            const std::size_t sample_offset =
                static_cast<std::size_t>(desktop) * 4;
            const auto sample_area =
                static_cast<unsigned long long>(
                    workareas[sample_offset + 2]) *
                workareas[sample_offset + 3];

            if (sample_area > best_area)
            {
                best_area = sample_area;
                best_workareas = workareas;
            }
        }

        if (sample + 1 < 20)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(10));
        }
    }

    if (best_workareas.empty())
    {
        m_x11_base_workarea = fallback;
        m_has_x11_base_workarea = true;
        return;
    }

    workareas = std::move(best_workareas);

    const std::size_t offset =
        static_cast<std::size_t>(desktop) * 4;
    const int root_x =
        static_cast<int>(workareas[offset]);
    const int root_y =
        static_cast<int>(workareas[offset + 1]);
    const int root_right = root_x +
        static_cast<int>(workareas[offset + 2]);
    const int root_bottom = root_y +
        static_cast<int>(workareas[offset + 3]);

    const int right = std::min(
        output.x + output.width,
        root_right);
    const int bottom = std::min(
        output.y + output.height,
        root_bottom);

    m_x11_base_workarea = {
        std::max(output.x, root_x),
        std::max(output.y, root_y),
        std::max(1, right - std::max(output.x, root_x)),
        std::max(1, bottom - std::max(output.y, root_y))};
    m_has_x11_base_workarea = true;

    g_message(
        "X11 base work area: %d,%d %dx%d",
        m_x11_base_workarea.x,
        m_x11_base_workarea.y,
        m_x11_base_workarea.width,
        m_x11_base_workarea.height);
}

void DockWindow::prepare_x11_monitor_change()
{
    if (m_uses_layer_shell)
        return;

    m_has_x11_base_workarea = false;

    if (!get_realized())
        return;

    auto gdk_window = get_window();
    auto display = get_display();
    if (!gdk_window || !display ||
        !GDK_IS_X11_DISPLAY(display->gobj()))
    {
        return;
    }

    Display *xdisplay =
        gdk_x11_display_get_xdisplay(display->gobj());
    const ::Window xid =
        gdk_x11_window_get_xid(gdk_window->gobj());

    XDeleteProperty(
        xdisplay,
        xid,
        XInternAtom(
            xdisplay,
            "_NET_WM_STRUT",
            False));
    XDeleteProperty(
        xdisplay,
        xid,
        XInternAtom(
            xdisplay,
            "_NET_WM_STRUT_PARTIAL",
            False));
    XFlush(xdisplay);
}

void DockWindow::apply_x11_strut(
    const DockPlacement &placement,
    int x,
    int y,
    int width,
    int height)
{
    if (!get_realized())
        return;

    auto gdk_window = get_window();
    auto display = get_display();
    if (!gdk_window || !display ||
        !GDK_IS_X11_DISPLAY(display->gobj()))
    {
        return;
    }

    Display *xdisplay =
        gdk_x11_display_get_xdisplay(display->gobj());
    const ::Window xid =
        gdk_x11_window_get_xid(gdk_window->gobj());
    const Atom strut = XInternAtom(
        xdisplay, "_NET_WM_STRUT", False);
    const Atom strut_partial = XInternAtom(
        xdisplay, "_NET_WM_STRUT_PARTIAL", False);

    unsigned long values[12] = {};
    if (placement.exclusive_zone < 0)
    {
        const int screen_width = DisplayWidth(
            xdisplay, DefaultScreen(xdisplay));
        const int screen_height = DisplayHeight(
            xdisplay, DefaultScreen(xdisplay));

        if (placement.is_vertical() &&
            placement.anchor_left)
        {
            values[0] = static_cast<unsigned long>(std::max(0, x + width));
            values[4] = static_cast<unsigned long>(std::max(0, y));
            values[5] = static_cast<unsigned long>(std::max(0, y + height - 1));
        }
        else if (placement.is_vertical() &&
                 placement.anchor_right)
        {
            values[1] = static_cast<unsigned long>(std::max(0, screen_width - x));
            values[6] = static_cast<unsigned long>(std::max(0, y));
            values[7] = static_cast<unsigned long>(std::max(0, y + height - 1));
        }
        else if (placement.is_horizontal() &&
                 placement.anchor_top)
        {
            values[2] = static_cast<unsigned long>(std::max(0, y + height));
            values[8] = static_cast<unsigned long>(std::max(0, x));
            values[9] = static_cast<unsigned long>(std::max(0, x + width - 1));
        }
        else if (placement.is_horizontal() &&
                 placement.anchor_bottom)
        {
            values[3] = static_cast<unsigned long>(std::max(0, screen_height - y));
            values[10] = static_cast<unsigned long>(std::max(0, x));
            values[11] = static_cast<unsigned long>(std::max(0, x + width - 1));
        }

        XChangeProperty(xdisplay, xid, strut, XA_CARDINAL, 32,
                        PropModeReplace,
                        reinterpret_cast<unsigned char *>(values), 4);
        XChangeProperty(xdisplay, xid, strut_partial, XA_CARDINAL, 32,
                        PropModeReplace,
                        reinterpret_cast<unsigned char *>(values), 12);
    }
    else
    {
        XDeleteProperty(xdisplay, xid, strut);
        XDeleteProperty(xdisplay, xid, strut_partial);
    }

    XFlush(xdisplay);
}

void DockWindow::apply_dock_orientation(
    DockOrientation orientation)
{
    if (orientation ==
        DockOrientation::vertical)
    {
        m_dock_box.set_orientation(
            Gtk::ORIENTATION_VERTICAL);
    }
    else
    {
        m_dock_box.set_orientation(
            Gtk::ORIENTATION_HORIZONTAL);
    }

    apply_main_axis_end_margins(
        orientation);
}

void DockWindow::apply_visual_style()
{
    auto dock_context =
        m_dock_box.get_style_context();

    auto window_context =
        get_style_context();

    const auto &layout_request =
        m_controller->layout_request();

    if (layout_request.rounded_corners)
    {
        dock_context->add_class(
            "dock-rounded");
        window_context->add_class(
            "dock-rounded");
    }
    else
    {
        dock_context->remove_class(
            "dock-rounded");
        window_context->remove_class(
            "dock-rounded");
    }

    const int configured_radius =
        layout_request.corner_radius;

    const int derived_radius =
        DockLayoutMetrics::corner_radius_for(
            m_effective_icon_size);

    const int effective_radius =
        layout_request.rounded_corners
            ? std::max(
                  0,
                  configured_radius < 0
                      ? derived_radius
                      : configured_radius)
            : 0;

    const std::string background_css =
        m_controller->settings()
                .gradient_background()
            ? " background-color: black;"
              " background-image: linear-gradient("
              "to top, #000000 0, #000000 2px, "
              "#413f3f 90%);"
            : " background-color: @theme_bg_color;"
              " background-image: none;";

    m_visual_css->load_from_data(
        "window.dock-window {"
        " background-color: transparent;"
        " border-radius: " +
        std::to_string(effective_radius) +
        "px;"
        "}"
        ".dock-surface {" +
        background_css +
        " border-radius: " +
        std::to_string(effective_radius) +
        "px;"
        "}");

    for (auto *item : dock_items())
    {
        item->set_context_menu_corner_radius(
            effective_radius);
    }

    if (m_home_item)
    {
        m_home_item
            ->set_context_menu_corner_radius(
                effective_radius);
    }

    m_overlay_window.set_rounded_corners(
        layout_request.rounded_corners,
        effective_radius,
        m_effective_icon_size);

    m_controller->set_preview_rounded_corners(
        layout_request.rounded_corners,
        effective_radius);
}

void DockWindow::apply_main_axis_end_margins(
    DockOrientation orientation)
{
    const bool horizontal =
        orientation ==
        DockOrientation::horizontal;

    const int leading_width =
        horizontal
            ? m_leading_main_axis_margin
            : 0;

    const int leading_height =
        horizontal
            ? 0
            : m_leading_main_axis_margin;

    const int trailing_width =
        horizontal
            ? m_trailing_main_axis_margin
            : 0;

    const int trailing_height =
        horizontal
            ? 0
            : m_trailing_main_axis_margin;

    // These spacers are children of m_dock_box, so GTK includes them in the
    // natural size used by DockLayoutEngine. This avoids a separate margin
    // calculation that could disagree with item and tooltip coordinates.
    m_leading_margin.set_size_request(
        leading_width,
        leading_height);

    m_trailing_margin.set_size_request(
        trailing_width,
        trailing_height);
}

std::vector<DockItem *>
DockWindow::dock_items()
{
    std::vector<DockItem *> items;

    for (auto *child :
         m_dock_box.get_children())
    {
        if (auto *item =
                dynamic_cast<DockItem *>(child))
        {
            items.push_back(item);
        }
    }

    return items;
}

Glib::RefPtr<Gio::AppInfo>
DockWindow::application_for_running(
    const std::string &desktop_id) const
{
    auto app =
        m_launcher_manager
            .find_application(
                desktop_id);

    if (app)
        return app;

    std::string display_name =
        desktop_id;

    if (m_window_registry)
    {
        const auto normalized_id =
            LauncherManager::
                normalize_desktop_id(
                    desktop_id);

        const auto window =
            std::find_if(
                m_window_registry
                    ->windows()
                    .begin(),
                m_window_registry
                    ->windows()
                    .end(),
                [&normalized_id](
                    const ManagedWindow
                        &candidate)
                {
                    return LauncherManager::
                               normalize_desktop_id(
                                   candidate
                                       .desktop_file_name) ==
                           normalized_id;
                });

        if (window !=
                m_window_registry
                    ->windows()
                    .end() &&
            !window->caption.empty())
        {
            display_name =
                window->caption;
        }
    }

    auto command =
        LauncherManager::
            normalize_desktop_id(
                desktop_id);

    constexpr char suffix[] =
        ".desktop"; // Desktop-entry filename suffix

    if (command.size() >=
        sizeof(suffix) - 1)
    {
        command.erase(
            command.size() -
            (sizeof(suffix) - 1));
    }

    try
    {
        return Gio::AppInfo::
            create_from_commandline(
                command,
                display_name,
                Gio::APP_INFO_CREATE_NONE);
    }
    catch (const Glib::Error &error)
    {
        g_warning(
            "Cannot create a dock item for running application '%s': %s",
            desktop_id.c_str(),
            error.what().c_str());
        return {};
    }
}

void DockWindow::schedule_dock_item_sync()
{
    if (m_dock_item_sync.connected())
        return;

    m_dock_item_sync =
        Glib::signal_idle().connect(
            [this]()
            {
                synchronize_dock_items();
                return false;
            });
}

void DockWindow::synchronize_dock_items()
{
    struct DesiredItem
    {
        std::string desktop_id;
        Glib::RefPtr<Gio::AppInfo> app;
        bool attached = false;
    };

    std::vector<DesiredItem> desired_items;

    const auto attached_ids =
        m_launcher_manager
            .attached_ids();

    std::vector<std::string>
        normalized_attached_ids;
    std::vector<std::string>
        normalized_running_ids;

    for (const auto &desktop_id :
         attached_ids)
    {
        normalized_attached_ids
            .push_back(
                m_launcher_manager
                    .normalize_resolved_id(
                        desktop_id));
    }

    if (m_window_registry)
    {
        for (const auto &running :
             m_window_registry
                 ->running_applications())
        {
            normalized_running_ids
                .push_back(
                    LauncherManager::
                        normalize_desktop_id(
                            running
                                .desktop_file_name));
        }
    }

    std::sort(
        normalized_running_ids.begin(),
        normalized_running_ids.end());

    normalized_running_ids.erase(
        std::unique(
            normalized_running_ids.begin(),
            normalized_running_ids.end()),
        normalized_running_ids.end());

    if (m_has_synchronized_items &&
        normalized_attached_ids ==
            m_synchronized_attached_ids &&
        normalized_running_ids ==
            m_synchronized_running_ids)
    {
        return;
    }

    m_synchronized_attached_ids =
        normalized_attached_ids;
    m_synchronized_running_ids =
        normalized_running_ids;
    m_has_synchronized_items = true;

    const int maximum_items =
        std::max(
            0,
            DockConstants::MAX_DOCK_ITEMS -
                1);

    const auto current_items =
        dock_items();

    // Keep the live visual order, including positions where running,
    // unattached applications have been dropped between attached launchers.
    for (auto *item : current_items)
    {
        if (static_cast<int>(
                desired_items.size()) >=
            maximum_items)
        {
            break;
        }

        const auto normalized_id =
            LauncherManager::
                normalize_desktop_id(
                    item->desktop_id());

        const bool attached =
            std::find(
                normalized_attached_ids
                    .begin(),
                normalized_attached_ids
                    .end(),
                normalized_id) !=
            normalized_attached_ids.end();

        const bool running =
            item->running();

        if (!attached && !running)
            continue;

        const bool already_present =
            std::any_of(
                desired_items.begin(),
                desired_items.end(),
                [&normalized_id](
                    const DesiredItem
                        &candidate)
                {
                    return LauncherManager::
                               normalize_desktop_id(
                                   candidate
                                       .desktop_id) ==
                           normalized_id;
                });

        if (already_present)
            continue;

        desired_items.push_back(
            {item->desktop_id(),
             {},
             attached});
    }

    for (const auto &desktop_id :
         attached_ids)
    {
        if (static_cast<int>(
                desired_items.size()) >=
            maximum_items)
        {
            break;
        }

        auto app =
            m_launcher_manager
                .find_application(
                    desktop_id);

        auto normalized_id =
            m_launcher_manager
                .normalize_resolved_id(
                    desktop_id);

        if (!app &&
            std::binary_search(
                normalized_running_ids
                    .begin(),
                normalized_running_ids
                    .end(),
                normalized_id))
        {
            app =
                application_for_running(
                    desktop_id);
        }

        if (!app)
        {
            g_warning(
                "Attached launcher '%s' is not installed",
                desktop_id.c_str());
            continue;
        }

        const auto canonical_id =
            !app->get_id().empty()
                ? app->get_id()
                : desktop_id;

        normalized_id =
            LauncherManager::
                normalize_desktop_id(
                    canonical_id);

        const bool already_present =
            std::any_of(
                desired_items.begin(),
                desired_items.end(),
                [&normalized_id](
                    const DesiredItem
                        &candidate)
                {
                    return LauncherManager::
                               normalize_desktop_id(
                                   candidate
                                       .desktop_id) ==
                           normalized_id;
                });

        if (already_present)
            continue;

        desired_items.push_back(
            {canonical_id,
             std::move(app),
             true});
    }

    if (m_window_registry)
    {
        for (const auto &running :
             m_window_registry
                 ->running_applications())
        {
            if (static_cast<int>(
                    desired_items.size()) >=
                maximum_items)
            {
                break;
            }

            auto app =
                application_for_running(
                    running
                        .desktop_file_name);

            if (!app)
                continue;

            const auto canonical_id =
                !app->get_id().empty()
                    ? app->get_id()
                    : running
                          .desktop_file_name;

            const auto normalized_id =
                LauncherManager::
                    normalize_desktop_id(
                        canonical_id);

            const bool already_present =
                std::any_of(
                    desired_items.begin(),
                    desired_items.end(),
                    [&normalized_id](
                        const DesiredItem
                            &candidate)
                    {
                        return LauncherManager::
                                   normalize_desktop_id(
                                       candidate
                                           .desktop_id) ==
                               normalized_id;
                    });

            if (already_present)
                continue;

            desired_items.push_back(
                {canonical_id,
                 std::move(app),
                 false});
        }
    }

    auto existing_items =
        dock_items();

    std::vector<DockItem *>
        ordered_items;

    bool children_changed = false;

    for (const auto &desired :
         desired_items)
    {
        const auto normalized_id =
            LauncherManager::
                normalize_desktop_id(
                    desired.desktop_id);

        const auto existing =
            std::find_if(
                existing_items.begin(),
                existing_items.end(),
                [&normalized_id](
                    DockItem *item)
                {
                    return LauncherManager::
                               normalize_desktop_id(
                                   item
                                       ->desktop_id()) ==
                           normalized_id;
                });

        DockItem *item = nullptr;

        if (existing !=
            existing_items.end())
        {
            item = *existing;
            existing_items.erase(
                existing);
            item->set_attached(
                desired.attached);
        }
        else
        {
            auto app = desired.app;

            if (!app)
            {
                app =
                    application_for_running(
                        desired.desktop_id);
            }

            if (!app)
                continue;

            item =
                Gtk::manage(
                    new DockItem(
                        *this,
                        app,
                        desired.desktop_id,
                        desired.attached,
                        m_window_registry,
                        m_effective_icon_size > 0
                            ? m_effective_icon_size
                            : m_controller
                                  ->settings()
                                  .icon_size(),
                        m_controller
                            ->settings()
                            .hover_effect(),
                        m_controller
                            ->settings()
                            .indicator(),
                        m_controller
                            ->settings()
                            .indicator_color()));

            item->set_manage_all_workspaces(
                m_controller
                    ->settings()
                    .manage_all_workspaces());

            m_dock_box.pack_start(
                *item,
                Gtk::PACK_SHRINK);
            item->show();
            children_changed = true;
        }

        ordered_items.push_back(item);
    }

    if (!existing_items.empty())
        hide_tooltip_immediately();

    for (auto *item : existing_items)
    {
        m_dock_box.remove(*item);
        children_changed = true;
    }

    int position = 2;

    for (auto *item : ordered_items)
    {
        m_dock_box.reorder_child(
            *item,
            position++);
    }

    m_dock_box.reorder_child(
        m_trailing_margin,
        -1);

    if (children_changed)
    {
        m_controller->dock_items_changed();
    }
    else if (m_effective_icon_size > 0)
    {
        apply_visual_style();
    }
}

// Creates the persistent dock container and its initial items after the
// controller is available. Separating construction from the window
// constructor also gives later synchronization a single widget setup path.
void DockWindow::create_dock()
{
    m_dock_box.pack_start(
        m_leading_margin,
        Gtk::PACK_SHRINK);

    m_home_item =
        Gtk::manage(
            new DockHomeItem(
                *this,
                m_window_registry,
                m_controller
                    ->settings()
                    .icon_size(),
                m_controller
                    ->settings()
                    .home_icon_path()));

    m_dock_box.pack_start(
        *m_home_item,
        Gtk::PACK_SHRINK);

    m_dock_box.pack_start(
        m_trailing_margin,
        Gtk::PACK_SHRINK);

    add(m_dock_box);
    synchronize_dock_items();
    m_dock_box.show_all();

    if (!m_controller
             ->settings()
             .home_icon_enabled())
    {
        m_home_item->hide();
    }
}
