// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// legacy_dock_surface_backend.cpp
//
// Implementation overview:
// Owns ordinary GTK toplevel placement and the legacy X11 work-area/strut
// integration while exposing current GDK monitor geometry.
//
// ------------------------------------------------------------

#include "legacy_dock_surface_backend.h"

#include "dock/dock_window.h"
#include "layout/dock_layout_geometry.h"

#include <gdk/gdkwayland.h>
#include <gdk/gdkx.h>
#include <X11/Xatom.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
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
    const auto value = std::getenv(name);
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

bool is_gnome_x11_session()
{
    return environment_contains(
               "XDG_SESSION_TYPE",
               "x11") &&
           (environment_contains(
                "XDG_CURRENT_DESKTOP",
                "gnome") ||
            environment_contains(
                "XDG_SESSION_DESKTOP",
                "gnome"));
}

bool uses_gnome_wayland_autohide_effect()
{
    if (!environment_contains(
            "XDG_SESSION_TYPE",
            "wayland"))
    {
        return false;
    }

    // XDG_CURRENT_DESKTOP describes the active desktop. Consult the session
    // fallback only when it is absent, because inherited session metadata can
    // otherwise misclassify an explicit Plasma XWayland presentation.
    const auto current_desktop =
        std::getenv("XDG_CURRENT_DESKTOP");
    return current_desktop && *current_desktop
               ? environment_contains(
                     "XDG_CURRENT_DESKTOP",
                     "gnome")
               : environment_contains(
                     "XDG_SESSION_DESKTOP",
                     "gnome");
}

bool is_kde_wayland_session()
{
    return environment_contains(
               "XDG_SESSION_TYPE",
               "wayland") &&
           (environment_contains(
                "XDG_CURRENT_DESKTOP",
                "kde") ||
            environment_contains(
                "XDG_CURRENT_DESKTOP",
                "plasma") ||
            environment_contains(
                "XDG_SESSION_DESKTOP",
                "kde") ||
            environment_contains(
                "XDG_SESSION_DESKTOP",
                "plasma") ||
            environment_contains(
                "KDE_FULL_SESSION",
                "true"));
}

bool is_cinnamon_x11_session()
{
    return environment_contains(
               "XDG_SESSION_TYPE",
               "x11") &&
           (environment_contains(
                "XDG_CURRENT_DESKTOP",
                "cinnamon") ||
            environment_contains(
                "XDG_SESSION_DESKTOP",
                "cinnamon"));
}

}

LegacyDockSurfaceBackend::
    LegacyDockSurfaceBackend(
        DockWindow &window,
        const Glib::RefPtr<Gdk::Monitor>
            &monitor)
    : m_window(window),
      m_monitor(monitor)
{
    auto display = m_window.get_display();
    m_native_x11 =
        display &&
        GDK_IS_X11_DISPLAY(display->gobj());
    m_ordinary_wayland =
        display &&
        GDK_IS_WAYLAND_DISPLAY(display->gobj());

    // Ordinary EWMH dock windows must carry these hints before realization
    // so the first explicit placement and strut are honored.
    m_window.set_type_hint(
        Gdk::WINDOW_TYPE_HINT_DOCK);
    m_window.set_skip_taskbar_hint(true);
    m_window.set_skip_pager_hint(true);
    m_window.set_keep_above(true);
    m_window.stick();
    m_window.set_position(Gtk::WIN_POS_NONE);

    // Mutter can paint the provisional centered toplevel before the Shell
    // integration commits its edge placement.
    if (m_ordinary_wayland &&
        is_gnome_wayland_session())
    {
        m_initial_placement_pending = true;
        m_window.set_opacity(0.0);
    }
}

void LegacyDockSurfaceBackend::set_monitor(
    const Glib::RefPtr<Gdk::Monitor>
        &monitor)
{
    m_monitor = monitor;
}

MonitorGeometry
LegacyDockSurfaceBackend::output_geometry() const
{
    DockLayoutGeometry geometry;
    return geometry.output_geometry(m_monitor);
}

MonitorGeometry
LegacyDockSurfaceBackend::work_area() const
{
    DockLayoutGeometry geometry;
    return geometry.monitor_geometry(m_monitor);
}

MonitorGeometry
LegacyDockSurfaceBackend::effective_work_area(
    const MonitorGeometry &output,
    const MonitorGeometry &work_area)
{
    capture_x11_base_workarea(
        output,
        work_area);

    return m_has_x11_base_workarea
               ? m_x11_base_workarea
               : work_area;
}

void LegacyDockSurfaceBackend::
    apply_dock_placement(
        const DockPlacement &placement,
        const MonitorGeometry &output,
        const MonitorGeometry &work_area)
{
    capture_x11_base_workarea(
        output,
        work_area);

    gtk_widget_set_size_request(
        GTK_WIDGET(m_window.gobj()),
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
        x = output.x + placement.margin_left;
    }
    else if (placement.is_vertical() &&
             placement.anchor_right)
    {
        x = output.x + output.width -
            placement.margin_right - width;
    }
    else if (placement.anchor_left)
        x = output.x + placement.margin_left;
    else if (placement.anchor_right)
        x = output.x + output.width -
            placement.margin_right - width;

    if (placement.is_horizontal() &&
        placement.anchor_top)
    {
        y = output.y + placement.margin_top;
    }
    else if (placement.is_horizontal() &&
             placement.anchor_bottom)
    {
        y = output.y + output.height -
            placement.margin_bottom - height;
    }
    else if (placement.anchor_top)
        y = output.y + placement.margin_top;
    else if (placement.anchor_bottom)
        y = output.y + output.height -
            placement.margin_bottom - height;

    m_window.resize(width, height);
    m_window.move(x, y);
    apply_x11_strut(
        placement,
        x,
        y,
        width,
        height);
}

void LegacyDockSurfaceBackend::reserve_space(
    const DockPlacement &)
{
    // The legacy placement method still applies layer-shell exclusive zones
    // and X11 struts atomically with placement. This becomes a real boundary
    // when those implementations are extracted in later migration steps.
}

void LegacyDockSurfaceBackend::clear_reserved_space()
{
    m_has_x11_base_workarea = false;

    if (!m_window.get_realized())
        return;

    auto gdk_window = m_window.get_window();
    auto display = m_window.get_display();
    if (!gdk_window || !display ||
        !GDK_IS_X11_DISPLAY(display->gobj()))
    {
        return;
    }

    Display *xdisplay =
        gdk_x11_display_get_xdisplay(
            display->gobj());
    const ::Window xid =
        gdk_x11_window_get_xid(
            gdk_window->gobj());

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

bool LegacyDockSurfaceBackend::
    uses_native_placement() const
{
    return false;
}

bool LegacyDockSurfaceBackend::is_native_x11() const
{
    return m_native_x11;
}

bool LegacyDockSurfaceBackend::
    is_ordinary_wayland() const
{
    return m_ordinary_wayland;
}

DockAutohideEffect
LegacyDockSurfaceBackend::
    default_autohide_effect() const
{
    return uses_gnome_wayland_autohide_effect()
               ? DockAutohideEffect::gnome
               : DockAutohideEffect::slide;
}

bool LegacyDockSurfaceBackend::
    delegates_autohide_effect(
        DockAutohideEffect effect) const
{
    return uses_gnome_wayland_autohide_effect() &&
           (effect == DockAutohideEffect::gnome ||
            effect == DockAutohideEffect::fade);
}

double LegacyDockSurfaceBackend::
    autohide_fade_opacity() const
{
    return m_window.get_opacity();
}

void LegacyDockSurfaceBackend::
    set_autohide_fade_opacity(
        double opacity)
{
    m_window.set_opacity(opacity);
}

void LegacyDockSurfaceBackend::
    finish_autohide_fade(
        bool hidden)
{
    // Native X11 keeps its transparent hidden surface mapped to avoid a
    // compositor map flash. Ordinary Wayland completes in the same unmapped
    // state used by the existing legacy autohide path.
    if (hidden && !m_native_x11)
        m_window.hide();
}

bool LegacyDockSurfaceBackend::
    initial_placement_pending() const
{
    return m_initial_placement_pending;
}

void LegacyDockSurfaceBackend::
    complete_initial_placement()
{
    if (!m_initial_placement_pending)
        return;

    m_initial_placement_pending = false;
    m_window.set_opacity(1.0);
}

void LegacyDockSurfaceBackend::capture_x11_base_workarea(
    const MonitorGeometry &output,
    const MonitorGeometry &fallback)
{
    if (m_has_x11_base_workarea)
        return;

    auto display = m_window.get_display();
    if (!display ||
        !GDK_IS_X11_DISPLAY(display->gobj()))
    {
        return;
    }

    Display *xdisplay =
        gdk_x11_display_get_xdisplay(display->gobj());
    const ::Window root =
        DefaultRootWindow(xdisplay);

    const bool reusable_gnome_x11_workarea =
        is_gnome_x11_session() &&
        m_x11_base_workarea.width > 0 &&
        m_x11_base_workarea.height > 0 &&
        m_x11_base_output.x == output.x &&
        m_x11_base_output.y == output.y &&
        m_x11_base_output.width == output.width &&
        m_x11_base_output.height == output.height;

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

    MonitorGeometry root_workarea = fallback;
    if (!best_workareas.empty())
    {
        workareas = std::move(best_workareas);

        const std::size_t offset =
            static_cast<std::size_t>(desktop) * 4;
        root_workarea = {
            static_cast<int>(workareas[offset]),
            static_cast<int>(workareas[offset + 1]),
            static_cast<int>(workareas[offset + 2]),
            static_cast<int>(workareas[offset + 3])};
    }

    // Under GNOME, KDE Wayland, and Cinnamon X11, the compositor's GDK monitor
    // work area is authoritative. Their native shell panels are not X11 dock
    // clients, while root-global _NET_WORKAREA cannot identify which monitor
    // owns a panel. GNOME and Cinnamon expose the required per-monitor
    // rectangles through GDK; dropping that inset on a multi-monitor desktop
    // puts a top dock underneath the panel and mis-centres vertical docks.
    if (is_gnome_wayland_session() ||
        is_gnome_x11_session() ||
        is_kde_wayland_session() ||
        is_cinnamon_x11_session())
    {
        // Mutter updates GDK's work area asynchronously after a client
        // deletes its strut. During an edge change, reuse the panel-only
        // area already captured for this unchanged output instead of
        // treating DockLight's previous reservation as a native panel.
        if (!reusable_gnome_x11_workarea)
        {
            m_x11_base_workarea =
                x11_scoped_monitor_workarea(
                    output,
                    fallback);
        }
    }
    else
    {
        m_x11_base_workarea =
            x11_initial_monitor_workarea(
                output,
                root_workarea,
                gdk_display_get_n_monitors(
                    display->gobj()) > 1);
    }

    // KWin reserves only the Plasma panel's content thickness in
    // _NET_WORKAREA. A floating panel's X11 window can be taller because it
    // also contains its outside margin and shadow (for example, a 60 px
    // window with a 44 px top strut). Placing another dock at the reported
    // work-area edge then makes the two surfaces visibly overlap. Inspect
    // mapped dock clients which own a strut and keep our base area outside
    // their visible panel geometry. Plasma centres the strut-sized panel in
    // the larger X11 window, so half of the excess dimension is an outer gap
    // rather than occupied panel content.
    const Atom client_list_atom = XInternAtom(
        xdisplay, "_NET_CLIENT_LIST", False);
    const Atom strut_partial_atom = XInternAtom(
        xdisplay, "_NET_WM_STRUT_PARTIAL", False);
    const Atom kde_blur_region_atom = XInternAtom(
        xdisplay, "_KDE_NET_WM_BLUR_BEHIND_REGION", False);

    auto read_property =
        [xdisplay](
            ::Window window,
            Atom property,
            Atom requested_type,
            unsigned long requested,
            std::vector<unsigned long> &values)
        {
            Atom actual_type = None;
            int actual_format = 0;
            unsigned long item_count = 0;
            unsigned long bytes_after = 0;
            unsigned char *data = nullptr;

            const int status = XGetWindowProperty(
                xdisplay,
                window,
                property,
                0,
                static_cast<long>(requested),
                False,
                requested_type,
                &actual_type,
                &actual_format,
                &item_count,
                &bytes_after,
                &data);

            if (status != Success ||
                actual_type != requested_type ||
                actual_format != 32 ||
                !data)
            {
                if (data)
                    XFree(data);
                return false;
            }

            const auto *items =
                reinterpret_cast<unsigned long *>(data);
            values.assign(items, items + item_count);
            XFree(data);
            return true;
        };

    std::vector<unsigned long> clients;
    // A client can disappear between _NET_CLIENT_LIST and the property or
    // geometry request. Do not let that ordinary X11 race terminate DockLight.
    gdk_x11_display_error_trap_push(display->gobj());
    if (read_property(
            root,
            client_list_atom,
            XA_WINDOW,
            4096,
            clients))
    {
        ::Window own_window = None;
        if (m_window.get_realized())
        {
            auto gdk_window = m_window.get_window();
            if (gdk_window)
            {
                own_window = gdk_x11_window_get_xid(
                    gdk_window->gobj());
            }
        }

        const int output_right = output.x + output.width;
        const int output_bottom = output.y + output.height;
        int area_left = m_x11_base_workarea.x;
        int area_top = m_x11_base_workarea.y;
        int area_right = area_left + m_x11_base_workarea.width;
        int area_bottom = area_top + m_x11_base_workarea.height;

        auto ranges_overlap = [](int first_start,
                                 int first_end,
                                 int second_start,
                                 int second_end)
        {
            return first_start < second_end &&
                   second_start < first_end;
        };

        for (const auto client_value : clients)
        {
            const ::Window client =
                static_cast<::Window>(client_value);
            if (client == own_window)
                continue;

            std::vector<unsigned long> struts;
            if (!read_property(
                    client,
                    strut_partial_atom,
                    XA_CARDINAL,
                    12,
                    struts) ||
                struts.size() < 12)
            {
                continue;
            }

            XWindowAttributes attributes{};
            if (!XGetWindowAttributes(
                    xdisplay, client, &attributes) ||
                attributes.map_state != IsViewable)
            {
                continue;
            }

            int client_x = 0;
            int client_y = 0;
            ::Window child = None;
            if (!XTranslateCoordinates(
                    xdisplay,
                    client,
                    root,
                    0,
                    0,
                    &client_x,
                    &client_y,
                    &child))
            {
                continue;
            }

            const int client_right =
                client_x + attributes.width;
            const int client_bottom =
                client_y + attributes.height;
            const int horizontal_outer_gap =
                std::max(
                    0,
                    (attributes.width -
                     static_cast<int>(
                         std::max(struts[0], struts[1]))) /
                        2);
            const int vertical_outer_gap =
                std::max(
                    0,
                    (attributes.height -
                     static_cast<int>(
                         std::max(struts[2], struts[3]))) /
                        2);
            int visible_left =
                client_x + horizontal_outer_gap;
            int visible_top =
                client_y + vertical_outer_gap;
            int visible_right =
                client_right - horizontal_outer_gap;
            int visible_bottom =
                client_bottom - vertical_outer_gap;

            // Plasma publishes the painted panel region as local rectangles.
            // This remains accurate when a right/bottom EWMH strut is measured
            // from the far edge of a multi-monitor root window and therefore
            // cannot be interpreted as the panel's local thickness.
            std::vector<unsigned long> blur_region;
            if (read_property(
                    client,
                    kde_blur_region_atom,
                    XA_CARDINAL,
                    4096,
                    blur_region) &&
                blur_region.size() >= 4 &&
                blur_region.size() % 4 == 0)
            {
                int region_left = attributes.width;
                int region_top = attributes.height;
                int region_right = 0;
                int region_bottom = 0;
                bool has_region = false;

                for (std::size_t index = 0;
                     index + 3 < blur_region.size();
                     index += 4)
                {
                    const int rectangle_x =
                        static_cast<int>(blur_region[index]);
                    const int rectangle_y =
                        static_cast<int>(blur_region[index + 1]);
                    const int rectangle_width =
                        static_cast<int>(blur_region[index + 2]);
                    const int rectangle_height =
                        static_cast<int>(blur_region[index + 3]);

                    if (rectangle_width <= 0 ||
                        rectangle_height <= 0 ||
                        rectangle_x < 0 ||
                        rectangle_y < 0 ||
                        rectangle_x + rectangle_width >
                            attributes.width ||
                        rectangle_y + rectangle_height >
                            attributes.height)
                    {
                        continue;
                    }

                    region_left = std::min(
                        region_left,
                        rectangle_x);
                    region_top = std::min(
                        region_top,
                        rectangle_y);
                    region_right = std::max(
                        region_right,
                        rectangle_x + rectangle_width);
                    region_bottom = std::max(
                        region_bottom,
                        rectangle_y + rectangle_height);
                    has_region = true;
                }

                if (has_region)
                {
                    visible_left = client_x + region_left;
                    visible_top = client_y + region_top;
                    visible_right = client_x + region_right;
                    visible_bottom = client_y + region_bottom;
                }
            }

            if (struts[2] > 0 &&
                ranges_overlap(
                    static_cast<int>(struts[8]),
                    static_cast<int>(struts[9]) + 1,
                    output.x,
                    output_right) &&
                ranges_overlap(
                    client_x,
                    client_right,
                    output.x,
                    output_right))
            {
                area_top = std::max(
                    area_top,
                    std::min(
                        output_bottom,
                        visible_bottom));
            }

            if (struts[3] > 0 &&
                ranges_overlap(
                    static_cast<int>(struts[10]),
                    static_cast<int>(struts[11]) + 1,
                    output.x,
                    output_right) &&
                ranges_overlap(
                    client_x,
                    client_right,
                    output.x,
                    output_right))
            {
                area_bottom = std::min(
                    area_bottom,
                    std::max(
                        output.y,
                        visible_top));
            }

            if (struts[0] > 0 &&
                ranges_overlap(
                    static_cast<int>(struts[4]),
                    static_cast<int>(struts[5]) + 1,
                    output.y,
                    output_bottom) &&
                ranges_overlap(
                    client_y,
                    client_bottom,
                    output.y,
                    output_bottom))
            {
                area_left = std::max(
                    area_left,
                    std::min(
                        output_right,
                        visible_right));
            }

            if (struts[1] > 0 &&
                ranges_overlap(
                    static_cast<int>(struts[6]),
                    static_cast<int>(struts[7]) + 1,
                    output.y,
                    output_bottom) &&
                ranges_overlap(
                    client_y,
                    client_bottom,
                    output.y,
                    output_bottom))
            {
                area_right = std::min(
                    area_right,
                    std::max(
                        output.x,
                        visible_left));
            }
        }

        m_x11_base_workarea = {
            area_left,
            area_top,
            std::max(1, area_right - area_left),
            std::max(1, area_bottom - area_top)};
    }
    gdk_x11_display_error_trap_pop_ignored(display->gobj());

    m_has_x11_base_workarea = true;
    m_x11_base_output = output;

    g_message(
        "X11 base work area: %d,%d %dx%d",
        m_x11_base_workarea.x,
        m_x11_base_workarea.y,
        m_x11_base_workarea.width,
        m_x11_base_workarea.height);
}

void LegacyDockSurfaceBackend::apply_x11_strut(
    const DockPlacement &placement,
    int x,
    int y,
    int width,
    int height)
{
    if (!m_window.get_realized())
        return;

    auto gdk_window = m_window.get_window();
    auto display = m_window.get_display();
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

    const int screen_width = DisplayWidth(
        xdisplay, DefaultScreen(xdisplay));
    const int screen_height = DisplayHeight(
        xdisplay, DefaultScreen(xdisplay));
    const bool can_reserve_root_edge =
        placement.exclusive_zone < 0 &&
        x11_strut_reaches_root_edge(
            placement,
            x,
            y,
            width,
            height,
            screen_width,
            screen_height);

    unsigned long values[12] = {};
    if (can_reserve_root_edge)
    {
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
