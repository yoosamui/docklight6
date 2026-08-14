// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_item.cpp
//
// Implementation overview:
// Implements launcher rendering, interaction, menus, drag-and-drop,
// hover animation, and application actions.
//
// Important implementation decisions:
// - GTK event handlers coordinate UI state but delegate window policy.
// - Animation frames are cached and timers advance lightweight state.
// - Drag payloads use Docklight's private target and stable desktop IDs.
// - Icon geometry is converted to plain data before publication.
//
// ------------------------------------------------------------

#include "dock_item.h"
#include "dock_constants.h"
#include "rendering/dock_icon_renderer.h"
#include "layout/dock_layout_metrics.h"
#include "dock_window.h"
#include "windowing/window_registry.h"

#include <gio/gdesktopappinfo.h>
#include <glibmm/i18n.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{

    constexpr unsigned int ZOOM_FRAME_INTERVAL_MS = 16; // Delay between zoom frames
    constexpr unsigned int BLUR_FRAME_INTERVAL_MS = 16; // Delay between blur frames
    constexpr unsigned int PRIMARY_ACTION_EFFECT_INTERVAL_MS = 35;
    constexpr int PRIMARY_ACTION_EFFECT_FRAME_COUNT = 4;
    constexpr double PRIMARY_ACTION_EFFECT_MIN_OPACITY = 0.55;
    constexpr int CONTEXT_MENU_ICON_SIZE = 20; // Window icon size in menu rows
    constexpr int CONTEXT_MENU_TITLE_WIDTH = 48; // Maximum menu title width in characters
    constexpr double INDICATOR_THICKNESS = 2.0;  // Line height
    constexpr double INDICATOR_LINE_INSET = 1.0; // Shortens 1 px on each side
    constexpr double INDICATOR_DOT_RADIUS = 2.0; // Dot radius in pixels
    constexpr double INDICATOR_DOT_GAP = 6.0; // Space between paired dots
    constexpr double INDICATOR_PI = 3.14159265358979323846; // Circle angle calculation

    std::string desktop_badge_text(
        const std::vector<unsigned int>
            &desktop_numbers)
    {
        std::string text = "[ ";

        for (std::size_t index = 0;
             index < desktop_numbers.size();
             ++index)
        {
            if (index > 0)
                text += ", ";

            text += std::to_string(
                desktop_numbers[index]);
        }

        text += " ]";

        return text;
    }

    std::vector<std::string>
    application_identifiers(
        const Glib::RefPtr<Gio::AppInfo> &app,
        const std::string &desktop_id)
    {
        std::vector<std::string> identifiers;

        if (!app)
            return identifiers;

        const auto add_identifier =
            [&identifiers](
                const std::string &identifier)
        {
            if (identifier.empty() ||
                std::find(
                    identifiers.begin(),
                    identifiers.end(),
                    identifier) !=
                    identifiers.end())
            {
                return;
            }

            identifiers.push_back(identifier);
        };

        add_identifier(desktop_id);
        add_identifier(app->get_id());
        add_identifier(app->get_executable());

        const auto icon = app->get_icon();

        if (icon &&
            G_IS_THEMED_ICON(icon->gobj()))
        {
            const auto icon_names =
                g_themed_icon_get_names(
                    G_THEMED_ICON(
                        icon->gobj()));

            for (int index = 0;
                 icon_names &&
                 icon_names[index];
                 ++index)
            {
                add_identifier(
                    icon_names[index]);
            }
        }

        if (G_IS_DESKTOP_APP_INFO(app->gobj()))
        {
            const auto startup_wm_class =
                g_desktop_app_info_get_startup_wm_class(
                    G_DESKTOP_APP_INFO(
                        app->gobj()));

            if (startup_wm_class)
                add_identifier(startup_wm_class);
        }

        return identifiers;
    }

    bool has_single_main_window(
        const Glib::RefPtr<Gio::AppInfo> &app)
    {
        if (!app ||
            !G_IS_DESKTOP_APP_INFO(app->gobj()))
        {
            return false;
        }

        auto *desktop_app =
            G_DESKTOP_APP_INFO(
                app->gobj());

        // AppInfo objects created from a command line can use the
        // GDesktopAppInfo type without having a backing key file.
        if (!g_desktop_app_info_get_filename(
                desktop_app))
        {
            return false;
        }

        return g_desktop_app_info_get_boolean(
                   desktop_app,
                   "SingleMainWindow") ||
               g_desktop_app_info_get_boolean(
                   desktop_app,
                   "X-GNOME-SingleWindow");
    }

    const char *new_window_action(
        GDesktopAppInfo *desktop_app)
    {
        if (!desktop_app)
            return nullptr;

        const auto *actions =
            g_desktop_app_info_list_actions(
                desktop_app);

        // Desktop action identifiers are chosen by each application.
        // Prefer the widely used freedesktop-style spelling, while also
        // supporting identifiers used by Firefox and Visual Studio Code.
        constexpr const char *candidates[] = { // Known desktop new-window action IDs
            "new-window",
            "NewWindow",
            "new-empty-window"};

        for (const auto *candidate :
             candidates)
        {
            for (int index = 0;
                 actions && actions[index];
                 ++index)
            {
                if (g_str_equal(
                        actions[index],
                        candidate))
                {
                    return actions[index];
                }
            }
        }

        return nullptr;
    }

    Glib::RefPtr<Gdk::AppLaunchContext>
    application_launch_context(
        const Glib::RefPtr<Gio::AppInfo> &app)
    {
        const auto display =
            Gdk::Display::get_default();

        if (!display)
            return {};

        auto context =
            display->get_app_launch_context();

        if (!context)
            return {};

        context->set_timestamp(
            gtk_get_current_event_time());

        if (app)
            context->set_icon(app->get_icon());

        return context;
    }


}

DockItem::DockItem(
    DockWindow &dock,
    Glib::RefPtr<Gio::AppInfo> app,
    const std::string &desktop_id,
    bool attached,
    WindowRegistry *window_registry,
    int icon_size,
    DockHoverEffect hover_effect,
    DockIndicator indicator,
    const std::string
        &indicator_color)
    : m_dock(dock),
      m_app(app),
      m_desktop_id(desktop_id),
      m_application_controller(
          window_registry,
          application_identifiers(
              app,
              desktop_id)),
      m_hover_effect(hover_effect),
      m_indicator(indicator),
      m_attached(attached),
      m_single_main_window(
          has_single_main_window(app))
{
    const auto set_menu_label =
        [](Gtk::MenuItem &item,
           const Glib::ustring &text)
    {
        item.set_label(text);
        item.set_use_underline(true);
    };

    set_menu_label(
        m_attach_item,
        _("_Attach"));
    set_menu_label(
        m_open_new_window_item,
        _("_Open New Window"));
    set_menu_label(
        m_close_all_item,
        _("_Close All"));
    set_menu_label(
        m_minimize_item,
        _("_Minimize"));
    set_menu_label(
        m_unminimize_item,
        _("_Unminimize"));
    set_menu_label(
        m_maximize_item,
        _("_Maximize"));

    if (!m_indicator_color.set(
            indicator_color))
    {
        m_indicator_color.set(
            "#69aaff");
    }

    set_visible_window(false);

    add_events(
        Gdk::ENTER_NOTIFY_MASK |
        Gdk::LEAVE_NOTIFY_MASK |
        Gdk::BUTTON_PRESS_MASK |
        Gdk::BUTTON_RELEASE_MASK |
        Gdk::SCROLL_MASK |
        Gdk::SMOOTH_SCROLL_MASK);

    const std::vector<Gtk::TargetEntry>
        drag_targets = {
            Gtk::TargetEntry(
                DockConstants::
                    DOCK_ITEM_DRAG_TARGET,
                Gtk::TARGET_SAME_APP)};

    drag_source_set(
        drag_targets,
        Gdk::BUTTON1_MASK,
        Gdk::ACTION_MOVE);

    drag_dest_set(
        drag_targets,
        Gtk::DEST_DEFAULT_MOTION |
            Gtk::DEST_DEFAULT_HIGHLIGHT,
        Gdk::ACTION_MOVE);

    image.set_halign(Gtk::ALIGN_CENTER);
    image.set_valign(Gtk::ALIGN_CENTER);
    add(image);

    signal_popup_menu().connect(
        sigc::mem_fun(
            *this,
            &DockItem::on_popup_menu));

    signal_draw().connect(
        sigc::mem_fun(
            *this,
            &DockItem::draw_indicator),
        true);

    m_attach_item.set_active(
        m_attached);
    m_attach_item.set_sensitive(
        !LauncherManager::
            is_transient_window_id(
                m_desktop_id));
    initialize_context_menu();
    set_icon_size(icon_size);
    refresh_indicator();

    show_all_children();
}

DockItem::~DockItem()
{
    m_zoom_animation.disconnect();
    m_blur_animation.disconnect();
    m_primary_action_effect.disconnect();
    m_window_action_idle.disconnect();
    m_context_menu_button_press.disconnect();
    m_context_menu_map.disconnect();
    m_context_menu_unmap.disconnect();
}

void DockItem::set_icon_size(int icon_size)
{
    icon_size = std::max(1, icon_size);

    if (icon_size == m_icon_size)
        return;

    m_icon_size = icon_size;

    reload_icon();

    set_size_request(
        DockLayoutMetrics::item_size_for(icon_size),
        DockLayoutMetrics::item_size_for(icon_size));

    queue_draw();
}

void DockItem::publish_icon_geometry(
    const WindowIconGeometry &geometry)
{
    m_application_controller
        .set_icon_geometry(geometry);
}

ItemGeometry DockItem::icon_geometry()
{
    const auto allocation =
        image.get_allocation();

    ItemGeometry geometry;

    image.translate_coordinates(
        m_dock,
        0,
        0,
        geometry.x,
        geometry.y);

    geometry.width =
        allocation.get_width();
    geometry.height =
        allocation.get_height();
    geometry.center_x =
        geometry.x +
        geometry.width / 2;
    geometry.center_y =
        geometry.y +
        geometry.height / 2;

    return geometry;
}

void DockItem::set_hover_effect(
    DockHoverEffect effect)
{
    if (effect == m_hover_effect)
    {
        apply_hover_effect();
        return;
    }

    m_hover_effect = effect;

    if (m_hover_effect == DockHoverEffect::zoom)
    {
        create_zoom_frames();
    }
    else
    {
        m_zoom_animation.disconnect();
        m_zoom_frames.clear();
        m_zoom_frame = 0;
        m_zoom_target_frame = 0;
    }

    if (m_hover_effect == DockHoverEffect::blur)
    {
        create_blur_frames();
    }
    else
    {
        m_blur_animation.disconnect();
        m_blur_frames.clear();
        m_blur_frame = 0;
        m_blur_target_frame = 0;
    }

    apply_hover_effect();
}

void DockItem::set_indicator(
    DockIndicator indicator)
{
    if (indicator == m_indicator)
        return;

    m_indicator = indicator;
    queue_draw();
}

void DockItem::set_indicator_color(
    const std::string &color)
{
    Gdk::RGBA parsed;

    if (!parsed.set(color))
        return;

    m_indicator_color = parsed;
    queue_draw();
}

void DockItem::set_manage_all_workspaces(
    bool enabled)
{
    m_application_controller
        .set_manage_all_workspaces(
            enabled);
}

void DockItem::set_attached(
    bool attached)
{
    m_attached = attached;

    if (m_attach_item.get_active() ==
        attached)
    {
        return;
    }

    m_updating_attach_state = true;
    m_attach_item.set_active(
        attached);
    m_updating_attach_state = false;
}

void DockItem::refresh_indicator()
{
    const auto window_count =
        m_application_controller
            .window_count();

    if (window_count ==
        m_indicator_window_count)
    {
        return;
    }

    m_indicator_window_count =
        window_count;
    queue_draw();
}

bool DockItem::draw_indicator(
    const Cairo::RefPtr<Cairo::Context>
        &context)
{
    if (!context ||
        m_indicator_window_count == 0)
    {
        return false;
    }

    const auto allocation =
        get_allocation();

    const double width =
        allocation.get_width();
    const double height =
        allocation.get_height();

    if (width <= 0.0 || height <= 0.0)
        return false;

    context->save();
    context->set_source_rgba(
        m_indicator_color.get_red(),
        m_indicator_color.get_green(),
        m_indicator_color.get_blue(),
        m_indicator_color.get_alpha());

    if (m_indicator ==
        DockIndicator::lines)
    {
        const double length =
            std::min(
                width,
                std::max(
                    0.0,
                    static_cast<double>(
                        m_icon_size) -
                        2.0 *
                            INDICATOR_LINE_INSET));

        const double origin =
            (width - length) /
            2.0;

        const double position =
            height -
            INDICATOR_THICKNESS;

        const auto draw_segment =
            [&](double start,
                double segment_length)
        {
            context->rectangle(
                start,
                position,
                segment_length,
                INDICATOR_THICKNESS);
        };

        if (m_indicator_window_count == 1)
        {
            const double half_length =
                length / 2.0;

            draw_segment(
                origin +
                    (length - half_length) /
                        2.0,
                half_length);
        }
        else
        {
            draw_segment(origin, length);
        }

        context->fill();
    }
    else
    {
        const double radius =
            INDICATOR_DOT_RADIUS;

        const double edge_center =
            height - radius;

        const double main_center =
            width / 2.0;

        const auto draw_dot =
            [&](double center)
        {
            context->arc(
                center,
                edge_center,
                radius,
                0.0,
                2.0 *
                    INDICATOR_PI);
        };

        if (m_indicator_window_count == 1)
        {
            draw_dot(main_center);
        }
        else
        {
            const double offset =
                radius +
                INDICATOR_DOT_GAP /
                    2.0;

            draw_dot(
                main_center -
                offset);
            draw_dot(
                main_center +
                offset);
        }

        context->fill();
    }

    context->restore();

    return false;
}

void DockItem::set_context_menu_corner_radius(
    int corner_radius)
{
    if (!m_context_menu_css)
        return;

    m_context_menu_css->load_from_data(
        "window.dock-context-menu-popup,"
        "window.dock-context-menu-popup decoration {"
        " background-color: transparent;"
        " background-image: none;"
        " border-radius: " +
        std::to_string(
            std::max(0, corner_radius)) +
        "px;"
        "}"
        "menu.dock-context-menu {"
        " background-clip: padding-box;"
        " border-radius: " +
        std::to_string(
            std::max(0, corner_radius)) +
        "px;"
        "}");
}

void DockItem::reload_icon()
{
    auto icon = m_app->get_icon();
    auto icon_theme =
        Gtk::IconTheme::get_default();

    if (!icon_theme)
    {
        g_warning(
            "Cannot load icon for %s: no GTK icon theme",
            m_app->get_name().c_str());
        return;
    }

    Gtk::IconInfo icon_info;

    if (icon)
    {
        icon_info =
            icon_theme->lookup_icon(
                icon,
                m_icon_size,
                Gtk::ICON_LOOKUP_USE_BUILTIN);
    }

    if (!icon_info)
    {
        for (const auto &entry :
             m_application_controller
                 .window_entries())
        {
            if (entry.icon_name.empty())
                continue;

            icon_info =
                icon_theme->lookup_icon(
                    entry.icon_name,
                    m_icon_size,
                    Gtk::ICON_LOOKUP_USE_BUILTIN);

            if (icon_info)
                break;
        }
    }

    if (!icon_info)
    {
        icon_info =
            icon_theme->lookup_icon(
                "application-x-executable",
                m_icon_size,
                Gtk::ICON_LOOKUP_USE_BUILTIN);
    }

    if (!icon_info)
    {
        g_warning(
            "Cannot find an icon for %s in the current theme",
            m_app->get_name().c_str());
        return;
    }

    try
    {
        auto pixbuf =
            icon_info.load_icon();

        if (!pixbuf)
        {
            g_warning(
                "Cannot load icon for %s from the current theme",
                m_app->get_name().c_str());
            return;
        }

        const int pixbuf_width =
            pixbuf->get_width();

        const int pixbuf_height =
            pixbuf->get_height();

        if (pixbuf_width > m_icon_size ||
            pixbuf_height > m_icon_size)
        {
            const double scale =
                std::min(
                    static_cast<double>(m_icon_size) /
                        pixbuf_width,
                    static_cast<double>(m_icon_size) /
                        pixbuf_height);

            pixbuf =
                pixbuf->scale_simple(
                    std::max(
                        1,
                        static_cast<int>(
                            pixbuf_width * scale)),
                    std::max(
                        1,
                        static_cast<int>(
                            pixbuf_height * scale)),
                    Gdk::INTERP_BILINEAR);
        }

        m_icon_pixbuf = pixbuf;
        m_hover_pixbuf =
            DockIconRenderer::create_standard_hover(
                m_icon_pixbuf);

        if (m_hover_effect == DockHoverEffect::zoom)
        {
            create_zoom_frames();
        }
        else
        {
            m_zoom_animation.disconnect();
            m_zoom_frames.clear();
            m_zoom_frame = 0;
            m_zoom_target_frame = 0;
        }

        if (m_hover_effect == DockHoverEffect::blur)
        {
            create_blur_frames();
        }
        else
        {
            m_blur_animation.disconnect();
            m_blur_frames.clear();
            m_blur_frame = 0;
            m_blur_target_frame = 0;
        }

        apply_hover_effect();
    }
    catch (const Glib::Error &error)
    {
        // Keep the previously displayed pixbuf when the new theme contains
        // a broken icon. One bad asset must not leave an empty dock item.
        const auto error_message =
            error.what();

        g_warning(
            "Cannot reload icon for %s: %s",
            m_app->get_name().c_str(),
            error_message.c_str());
    }
}

Glib::ustring DockItem::app_name() const
{
    return m_app->get_display_name();
}

Glib::ustring DockItem::tooltip_text() const
{
    auto text = app_name();

    const auto window_count =
        m_application_controller
            .window_count();

    if (window_count > 1)
    {
        text += " (";
        text += std::to_string(
            window_count);
        text += ")";
    }

    return text;
}

std::vector<ApplicationWindowEntry>
DockItem::window_entries() const
{
    return m_application_controller
        .window_entries();
}

bool DockItem::show_window(
    const WindowId &window_id)
{
    return m_application_controller
        .show_window(window_id);
}

bool DockItem::minimize_window(
    const WindowId &window_id)
{
    return m_application_controller
        .minimize_window(window_id);
}

bool DockItem::close_window(
    const WindowId &window_id)
{
    return m_application_controller
        .close_window(window_id);
}

bool DockItem::toggle_window(
    const WindowId &window_id)
{
    return m_application_controller
        .toggle_window(window_id);
}

bool DockItem::on_enter_notify_event(
    GdkEventCrossing *event)
{
    if (m_dock.preview_input_forwarding())
        return false;

    if (event &&
        event->detail ==
            GDK_NOTIFY_INFERIOR)
    {
        return false;
    }

    if (!m_hovered)
    {
        m_application_controller
            .reset_window_cycle();
        m_scroll_delta_y = 0.0;
        m_hovered = true;
        apply_hover_effect();
    }

    m_dock.schedule_show_tooltip(*this);

    return true;
}

bool DockItem::on_leave_notify_event(
    GdkEventCrossing *event)
{
    if (m_dock.preview_input_forwarding())
        return false;

    if (event &&
        event->detail ==
            GDK_NOTIFY_INFERIOR)
    {
        return false;
    }

    if (m_hovered)
    {
        m_hovered = false;
        apply_hover_effect();
    }

    m_dock.schedule_hide_tooltip(*this);
    return false;
}

bool DockItem::on_scroll_event(
    GdkEventScroll *event)
{
    WindowCycleDirection direction;

    switch (event->direction)
    {
    case GDK_SCROLL_UP:
        m_scroll_delta_y = 0.0;
        direction =
            WindowCycleDirection::previous;
        break;

    case GDK_SCROLL_DOWN:
        m_scroll_delta_y = 0.0;
        direction =
            WindowCycleDirection::next;
        break;

    case GDK_SCROLL_SMOOTH:
        if (event->delta_y == 0.0)
            return false;

        m_scroll_delta_y +=
            event->delta_y;

        if (m_scroll_delta_y <= -1.0)
        {
            m_scroll_delta_y += 1.0;
            direction =
                WindowCycleDirection::
                    previous;
        }
        else if (m_scroll_delta_y >= 1.0)
        {
            m_scroll_delta_y -= 1.0;
            direction =
                WindowCycleDirection::next;
        }
        else
        {
            return true;
        }

        break;

    default:
        return false;
    }

    m_application_controller
        .cycle_window(direction);

    return true;
}

void DockItem::set_vertical(bool vertical)
{
    if (vertical)
        label.hide();
    else
        label.show();
}

bool DockItem::on_button_press_event(GdkEventButton *event)
{
    if (!event)
        return false;

    // A dock-icon press starts a new interaction. Close any preview at press
    // time instead of waiting for the launch, minimize, or menu action that
    // follows on release.
    m_dock.hide_tooltip_immediately();

    if (event->button == GDK_BUTTON_SECONDARY)
    {
        show_context_menu(
            reinterpret_cast<GdkEvent *>(event));
        return true;
    }

    if (event->button == GDK_BUTTON_PRIMARY)
    {
        const auto image_allocation =
            image.get_allocation();

        int image_x = 0;
        int image_y = 0;

        if (!image.translate_coordinates(
                *this,
                0,
                0,
                image_x,
                image_y))
        {
            image_x =
                image_allocation.get_x();
            image_y =
                image_allocation.get_y();
        }

        m_drag_pixbuf =
            image.get_pixbuf();

        if (!m_drag_pixbuf)
            m_drag_pixbuf =
                m_icon_pixbuf;

        int pixbuf_x = image_x;
        int pixbuf_y = image_y;

        if (m_drag_pixbuf)
        {
            pixbuf_x +=
                (image_allocation
                     .get_width() -
                 m_drag_pixbuf
                     ->get_width()) /
                2;
            pixbuf_y +=
                (image_allocation
                     .get_height() -
                 m_drag_pixbuf
                     ->get_height()) /
                2;
        }

        m_drag_hot_x =
            static_cast<int>(
                event->x) -
            pixbuf_x;
        m_drag_hot_y =
            static_cast<int>(
                event->y) -
            pixbuf_y;

        m_primary_button_pressed = true;
        return true;
    }

    return false;
}

bool DockItem::on_button_release_event(
    GdkEventButton *event)
{
    if (!event ||
        event->button !=
            GDK_BUTTON_PRIMARY ||
        !m_primary_button_pressed)
    {
        return false;
    }

    m_primary_button_pressed = false;

    if (m_dragging)
        return true;

    const auto now =
        g_get_monotonic_time();
    constexpr gint64 action_debounce_time =
        350 * 1000;

    if (m_last_primary_action_time != 0 &&
        now - m_last_primary_action_time <
            action_debounce_time)
    {
        return true;
    }

    m_last_primary_action_time = now;

    start_primary_action_effect();
    perform_primary_action();

    return true;
}

void DockItem::start_primary_action_effect()
{
    m_primary_action_effect.disconnect();
    m_primary_action_effect_frame = 0;

    image.set_opacity(
        PRIMARY_ACTION_EFFECT_MIN_OPACITY);

    m_primary_action_effect =
        Glib::signal_timeout().connect(
            sigc::mem_fun(
                *this,
                &DockItem::
                    advance_primary_action_effect),
            PRIMARY_ACTION_EFFECT_INTERVAL_MS);
}

bool DockItem::advance_primary_action_effect()
{
    ++m_primary_action_effect_frame;

    const double progress =
        static_cast<double>(
            m_primary_action_effect_frame) /
        static_cast<double>(
            PRIMARY_ACTION_EFFECT_FRAME_COUNT - 1);

    image.set_opacity(
        PRIMARY_ACTION_EFFECT_MIN_OPACITY +
        (1.0 -
         PRIMARY_ACTION_EFFECT_MIN_OPACITY) *
            std::min(1.0, progress));

    if (m_primary_action_effect_frame <
        PRIMARY_ACTION_EFFECT_FRAME_COUNT - 1)
    {
        return true;
    }

    image.set_opacity(1.0);

    return false;
}

void DockItem::perform_primary_action()
{
    if (!m_application_controller
             .running())
    {
        launch_application();
        return;
    }

    m_application_controller
        .toggle_minimized();
}

void DockItem::on_drag_begin(
    const Glib::RefPtr<
        Gdk::DragContext> &context)
{
    m_dragging = true;
    m_primary_button_pressed = false;
    m_dock.begin_item_drag(*this);

    if (m_drag_pixbuf)
    {
        gtk_drag_set_icon_pixbuf(
            context->gobj(),
            m_drag_pixbuf->gobj(),
            m_drag_hot_x,
            m_drag_hot_y);
    }
}

void DockItem::on_drag_end(
    const Glib::RefPtr<
        Gdk::DragContext> &)
{
    m_dock.end_item_drag(*this);
    m_dragging = false;
    m_primary_button_pressed = false;
    m_drag_pixbuf.reset();
}

void DockItem::on_drag_data_get(
    const Glib::RefPtr<
        Gdk::DragContext> &,
    Gtk::SelectionData &selection_data,
    guint,
    guint)
{
    selection_data.set(
        DockConstants::
            DOCK_ITEM_DRAG_TARGET,
        m_desktop_id);
}

bool DockItem::on_drag_motion(
    const Glib::RefPtr<
        Gdk::DragContext> &context,
    int,
    int,
    guint time)
{
    if (!m_dock.can_drop_item(*this))
        return false;

    context->drag_status(
        Gdk::ACTION_MOVE,
        time);
    return true;
}

bool DockItem::on_drag_drop(
    const Glib::RefPtr<
        Gdk::DragContext> &context,
    int x,
    int y,
    guint time)
{
    const bool accepted =
        m_dock.drop_item(
            *this,
            x,
            y);

    context->drag_finish(
        accepted,
        false,
        time);

    return accepted;
}

bool DockItem::on_popup_menu()
{
    show_context_menu(nullptr);
    return true;
}

void DockItem::initialize_context_menu()
{
    auto initialize_item =
        [](Gtk::MenuItem &item,
           bool bold = false)
    {
        item.set_halign(Gtk::ALIGN_FILL);
        item.set_valign(Gtk::ALIGN_CENTER);

        auto *label =
            dynamic_cast<Gtk::Label *>(
                item.get_child());

        if (!label)
            return;

        label->set_halign(Gtk::ALIGN_FILL);
        label->set_valign(Gtk::ALIGN_FILL);
        label->set_xalign(0.0F);
        label->set_yalign(0.5F);

        // Preserve the native GTK mnemonic while ensuring its underline
        // remains visible even when the desktop hides mouse-opened menu
        // mnemonics.
        Pango::AttrList attributes;
        auto underline =
            Pango::Attribute::
                create_attr_underline(
                    Pango::UNDERLINE_SINGLE);

        const auto mnemonic_index =
            item.get_label()
                .raw()
                .find('_');

        if (mnemonic_index ==
            std::string::npos)
        {
            return;
        }

        underline.set_start_index(
            static_cast<unsigned int>(
                mnemonic_index));
        underline.set_end_index(
            static_cast<unsigned int>(
                mnemonic_index + 1));
        attributes.insert(underline);

        if (bold)
        {
            auto weight =
                Pango::Attribute::
                    create_attr_weight(
                        Pango::WEIGHT_BOLD);
            attributes.insert(weight);
        }

        label->set_attributes(attributes);
    };

    initialize_item(m_attach_item);
    initialize_item(
        m_open_new_window_item,
        true);
    initialize_item(m_minimize_item);
    initialize_item(m_unminimize_item);
    initialize_item(m_maximize_item);
    initialize_item(m_close_all_item);

    m_context_menu.append(
        m_group_separator);
    m_context_menu.append(
        m_attach_item);
    m_context_menu.append(
        m_attach_separator);
    m_context_menu.append(
        m_open_new_window_item);
    m_context_menu.append(
        m_window_separator);
    m_context_menu.append(
        m_minimize_item);
    m_context_menu.append(
        m_unminimize_item);
    m_context_menu.append(
        m_maximize_item);
    m_context_menu.append(
        m_close_separator);
    m_context_menu.append(
        m_close_all_item);

    m_attach_item
        .signal_toggled()
        .connect(
            [this]()
            {
                if (m_updating_attach_state)
                    return;

                const bool requested =
                    m_attach_item
                        .get_active();

                if (!m_dock
                         .set_item_attached(
                             *this,
                             requested))
                {
                    set_attached(
                        m_attached);
                }
            });

    m_open_new_window_item
        .signal_activate()
        .connect(
            [this]()
            {
                launch_new_window();
            });

    m_close_all_item
        .signal_activate()
        .connect(
            [this]()
            {
                const bool accepted =
                    m_application_controller
                        .close_all();

                if (!accepted)
                {
                    g_warning(
                        "Close all windows rejected for %s",
                        m_app->get_id().c_str());
                }
            });

    m_minimize_item
        .signal_activate()
        .connect(
            [this]()
            {
                const bool accepted =
                    m_application_controller
                        .minimize();

                if (!accepted)
                {
                    g_warning(
                        "Minimize windows rejected for %s",
                        m_app->get_id().c_str());
                }
            });

    m_maximize_item
        .signal_activate()
        .connect(
            [this]()
            {
                const bool accepted =
                    m_application_controller
                        .maximize();

                if (!accepted)
                {
                    g_warning(
                        "Maximize window rejected for %s",
                        m_app->get_id().c_str());
                }
            });

    m_unminimize_item
        .signal_activate()
        .connect(
            [this]()
            {
                const bool accepted =
                    m_application_controller
                        .unminimize();

                if (!accepted)
                {
                    g_warning(
                        "Unminimize windows rejected for %s",
                        m_app->get_id().c_str());
                }
            });

    auto context =
        m_context_menu.get_style_context();

    context->add_class(
        "dock-context-menu");

    m_context_menu_css =
        Gtk::CssProvider::create();

    context->add_provider(
        m_context_menu_css,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION +
            1);

    m_context_menu.show_all();
    m_group_separator.hide();

    m_context_menu_button_press =
        m_context_menu
            .signal_button_press_event()
            .connect(
                [this](GdkEventButton *event)
                {
                    if (!event ||
                        event->button !=
                            GDK_BUTTON_SECONDARY)
                    {
                        return false;
                    }

                    // GtkMenu owns the pointer grab, so a secondary press on
                    // its owning DockItem arrives in menu coordinates outside
                    // the menu allocation instead of reaching the item.
                    const bool outside_menu =
                        event->x < 0.0 ||
                        event->y < 0.0 ||
                        event->x >=
                            m_context_menu
                                .get_allocated_width() ||
                        event->y >=
                            m_context_menu
                                .get_allocated_height();

                    if (!outside_menu)
                        return false;

                    m_context_menu_secondary_dismissed =
                        true;
                    m_context_menu.popdown();
                    return true;
                },
                false);

    m_context_menu_map =
        m_context_menu.signal_map().connect(
            [this]()
            {
                if (m_context_menu_mapped)
                    return;

                m_context_menu_mapped = true;
                m_context_menu_secondary_dismissed =
                    false;
                m_dock.inhibit_autohide();
            });

    m_context_menu_unmap =
        m_context_menu.signal_unmap().connect(
            [this]()
            {
                if (!m_context_menu_mapped)
                    return;

                m_context_menu_mapped = false;

                const bool reopen_preview =
                    m_context_menu_secondary_dismissed;
                m_context_menu_secondary_dismissed =
                    false;

                if (reopen_preview)
                    m_dock.schedule_show_tooltip(
                        *this);

                if (reopen_preview)
                    m_dock.uninhibit_autohide(true);
                else
                    m_dock.uninhibit_autohide();
            });
}

void DockItem::rebuild_window_menu_items()
{
    for (const auto &item :
         m_window_menu_items)
    {
        m_context_menu.remove(*item);
    }

    m_window_menu_items.clear();

    auto entries =
        m_application_controller
            .window_entries();

    m_window_menu_order.erase(
        std::remove_if(
            m_window_menu_order.begin(),
            m_window_menu_order.end(),
            [&entries](
                const WindowId &window_id)
            {
                return std::none_of(
                    entries.begin(),
                    entries.end(),
                    [&window_id](
                        const ApplicationWindowEntry
                            &entry)
                    {
                        return entry.id ==
                               window_id;
                    });
            }),
        m_window_menu_order.end());

    for (const auto &entry : entries)
    {
        if (std::find(
                m_window_menu_order.begin(),
                m_window_menu_order.end(),
                entry.id) ==
            m_window_menu_order.end())
        {
            m_window_menu_order.push_back(
                entry.id);
        }
    }

    std::vector<ApplicationWindowEntry>
        ordered_entries;

    ordered_entries.reserve(
        entries.size());

    for (const auto &window_id :
         m_window_menu_order)
    {
        const auto entry =
            std::find_if(
                entries.begin(),
                entries.end(),
                [&window_id](
                    const ApplicationWindowEntry
                        &candidate)
                {
                    return candidate.id ==
                           window_id;
                });

        if (entry != entries.end())
        {
            ordered_entries.push_back(
                std::move(*entry));
        }
    }

    int position = 0;

    for (const auto &entry :
         ordered_entries)
    {
        auto item =
            std::make_unique<
                Gtk::ImageMenuItem>();

        item->set_halign(
            Gtk::ALIGN_FILL);
        item->set_valign(
            Gtk::ALIGN_CENTER);
        item->set_hexpand(true);

        auto row =
            Gtk::manage(
                new Gtk::Box(
                    Gtk::ORIENTATION_HORIZONTAL,
                    8));

        row->set_halign(
            Gtk::ALIGN_FILL);
        row->set_valign(
            Gtk::ALIGN_CENTER);
        row->set_hexpand(true);

        auto icon =
            Gtk::manage(
                new Gtk::Image());

        icon->set_halign(
            Gtk::ALIGN_CENTER);
        icon->set_valign(
            Gtk::ALIGN_CENTER);
        icon->set_size_request(
            CONTEXT_MENU_ICON_SIZE,
            CONTEXT_MENU_ICON_SIZE);

        const auto pixbuf =
            entry.minimized
                ? context_menu_minimized_icon()
                : context_menu_window_icon(
                      entry.icon_name);

        if (pixbuf)
            icon->set(pixbuf);

        item->set_image(*icon);
        item->set_always_show_image(
            true);

        auto window_label =
            Gtk::manage(
                new Gtk::Label(
                    entry.caption.empty()
                        ? m_app
                              ->get_display_name()
                        : entry.caption));

        window_label->set_halign(
            Gtk::ALIGN_FILL);
        window_label->set_valign(
            Gtk::ALIGN_CENTER);
        window_label->set_hexpand(true);
        window_label->set_xalign(0.0F);
        window_label->set_yalign(0.5F);
        window_label->set_ellipsize(
            Pango::ELLIPSIZE_END);
        window_label->set_max_width_chars(
            CONTEXT_MENU_TITLE_WIDTH);

        if (!entry.on_current_desktop &&
            !entry.desktop_numbers.empty())
        {
            auto desktop_badge =
                Gtk::manage(
                    new Gtk::Label(
                        desktop_badge_text(
                            entry.desktop_numbers)));

            desktop_badge->set_halign(
                Gtk::ALIGN_CENTER);
            desktop_badge->set_valign(
                Gtk::ALIGN_CENTER);
            desktop_badge->set_xalign(0.5F);
            desktop_badge->set_yalign(0.5F);

            row->pack_start(
                *desktop_badge,
                false,
                false);
        }

        row->pack_start(
            *window_label,
            true,
            true);

        item->add(*row);

        const auto window_id =
            entry.id;
        const bool minimize =
            entry.active &&
            !entry.minimized;

        item->signal_activate()
            .connect(
                [this,
                 window_id,
                 minimize]()
                {
                    schedule_window_action(
                        window_id,
                        minimize);
                });

        m_context_menu.insert(
            *item,
            position++);

        item->show_all();

        m_window_menu_items.push_back(
            std::move(item));
    }

    if (entries.empty())
        m_group_separator.hide();
    else
        m_group_separator.show();
}

void DockItem::schedule_window_action(
    const WindowId &window_id,
    bool minimize)
{
    // Activating a KWin window while GtkMenu still owns its popup grab can
    // make GTK restore focus to the popup during teardown.  Close the menu
    // first and dispatch the window command on the next main-loop turn.
    m_context_menu.popdown();
    m_window_action_idle.disconnect();

    m_window_action_idle =
        Glib::signal_idle().connect(
            [this,
             window_id,
             minimize]()
            {
                const bool accepted =
                    minimize
                        ? m_application_controller
                              .minimize_window(
                                  window_id)
                        : m_application_controller
                              .show_window(
                                  window_id);

                if (!accepted)
                {
                    g_warning(
                        "%s window %s rejected for %s",
                        minimize
                            ? "Minimize"
                            : "Show",
                        window_id.c_str(),
                        m_app->get_id().c_str());
                }

                return false;
            });
}

void DockItem::show_context_menu(
    const GdkEvent *event)
{
    refresh_context_menu();

    Gdk::Gravity widget_anchor =
        Gdk::GRAVITY_NORTH;

    Gdk::Gravity menu_anchor =
        Gdk::GRAVITY_SOUTH;

    switch (m_dock.location())
    {
    case DockLocation::bottom:
        widget_anchor = Gdk::GRAVITY_NORTH;
        menu_anchor = Gdk::GRAVITY_SOUTH;
        break;

    case DockLocation::top:
        widget_anchor = Gdk::GRAVITY_SOUTH;
        menu_anchor = Gdk::GRAVITY_NORTH;
        break;

    case DockLocation::left:
        widget_anchor = Gdk::GRAVITY_EAST;
        menu_anchor = Gdk::GRAVITY_WEST;
        break;

    case DockLocation::right:
        widget_anchor = Gdk::GRAVITY_WEST;
        menu_anchor = Gdk::GRAVITY_EAST;
        break;
    }

    m_dock.hide_tooltip_immediately();

    m_context_menu.popup_at_widget(
        this,
        widget_anchor,
        menu_anchor,
        event);

    // GtkMenu is hosted in its own popup GtkWindow. GTK normally hides
    // mnemonic underlines for pointer-opened menus, so make them visible on
    // that popup window after GTK has created and mapped it.
    auto *menu_toplevel =
        gtk_widget_get_toplevel(
            GTK_WIDGET(
                m_context_menu.gobj()));

    if (GTK_IS_WINDOW(menu_toplevel))
    {
        auto *popup_context =
            gtk_widget_get_style_context(
                menu_toplevel);

        gtk_style_context_add_class(
            popup_context,
            "dock-context-menu-popup");

        gtk_style_context_add_provider(
            popup_context,
            GTK_STYLE_PROVIDER(
                m_context_menu_css->gobj()),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION +
                1);

        gtk_window_set_mnemonics_visible(
            GTK_WINDOW(menu_toplevel),
            TRUE);
    }
}

void DockItem::refresh_context_menu()
{
    rebuild_window_menu_items();

    m_open_new_window_item.set_sensitive(
        !m_application_controller.running() ||
        !m_single_main_window);

    m_minimize_item.set_sensitive(
        m_application_controller
            .can_minimize());

    m_unminimize_item.set_sensitive(
        m_application_controller
            .can_unminimize());

    m_maximize_item.set_sensitive(
        m_application_controller
            .can_maximize());

    m_close_all_item.set_sensitive(
        m_application_controller
            .can_close());
}

Glib::RefPtr<Gdk::Pixbuf>
DockItem::context_menu_window_icon(
    const std::string &icon_name) const
{
    const auto icon_theme =
        Gtk::IconTheme::get_default();

    if (icon_theme &&
        !icon_name.empty())
    {
        try
        {
            const auto icon =
                icon_theme->load_icon(
                    icon_name,
                    CONTEXT_MENU_ICON_SIZE,
                    Gtk::ICON_LOOKUP_USE_BUILTIN);

            if (icon)
                return icon;
        }
        catch (const Glib::Error &)
        {
        }
    }

    if (!m_icon_pixbuf)
        return {};

    const double scale =
        std::min(
            static_cast<double>(
                CONTEXT_MENU_ICON_SIZE) /
                m_icon_pixbuf->get_width(),
            static_cast<double>(
                CONTEXT_MENU_ICON_SIZE) /
                m_icon_pixbuf->get_height());

    return m_icon_pixbuf->scale_simple(
        std::max(
            1,
            static_cast<int>(
                m_icon_pixbuf->get_width() *
                scale)),
        std::max(
            1,
            static_cast<int>(
                m_icon_pixbuf->get_height() *
                scale)),
        Gdk::INTERP_BILINEAR);
}

Glib::RefPtr<Gdk::Pixbuf>
DockItem::context_menu_minimized_icon() const
{
    // Do not depend on an icon-theme name here. Several valid GTK themes do
    // not provide view-hidden or object-hidden-symbolic, which used to leave
    // a blank space in the dynamic window menu. Drawing the small symbolic
    // eye locally also lets it follow the menu foreground on light and dark
    // themes.
    auto surface =
        Cairo::ImageSurface::create(
            Cairo::FORMAT_ARGB32,
            CONTEXT_MENU_ICON_SIZE,
            CONTEXT_MENU_ICON_SIZE);
    auto context =
        Cairo::Context::create(surface);

    context->set_operator(
        Cairo::OPERATOR_SOURCE);
    context->set_source_rgba(
        0.0,
        0.0,
        0.0,
        0.0);
    context->paint();
    context->set_operator(
        Cairo::OPERATOR_OVER);

    const auto color =
        m_context_menu
            .get_style_context()
            ->get_color(
                Gtk::STATE_FLAG_NORMAL);

    context->set_source_rgba(
        color.get_red(),
        color.get_green(),
        color.get_blue(),
        color.get_alpha());
    context->set_line_width(1.7);
    context->set_line_cap(
        Cairo::LINE_CAP_ROUND);
    context->set_line_join(
        Cairo::LINE_JOIN_ROUND);

    context->move_to(2.5, 10.0);
    context->curve_to(
        5.8, 5.5,
        14.2, 5.5,
        17.5, 10.0);
    context->curve_to(
        14.2, 14.5,
        5.8, 14.5,
        2.5, 10.0);
    context->stroke();

    context->arc(
        10.0,
        10.0,
        2.2,
        0.0,
        2.0 * INDICATOR_PI);
    context->fill();

    // A diagonal stroke distinguishes the minimized state from a generic
    // visibility icon without relying on a theme-specific symbolic asset.
    context->move_to(3.5, 3.5);
    context->line_to(16.5, 16.5);
    context->stroke();

    surface->flush();

    return Gdk::Pixbuf::create(
        surface,
        0,
        0,
        CONTEXT_MENU_ICON_SIZE,
        CONTEXT_MENU_ICON_SIZE);
}

void DockItem::launch_application()
{
    try
    {
        std::vector<
            Glib::RefPtr<Gio::File>>
            files;

        m_app->launch(
            files,
            application_launch_context(
                m_app));
    }
    catch (const Glib::Error &error)
    {
        g_warning(
            "Cannot launch %s: %s",
            m_app->get_name().c_str(),
            error.what().c_str());
    }
}

void DockItem::launch_new_window()
{
    if (G_IS_DESKTOP_APP_INFO(
            m_app->gobj()))
    {
        auto *desktop_app =
            G_DESKTOP_APP_INFO(
                m_app->gobj());

        const auto *action =
            new_window_action(
                desktop_app);

        if (action)
        {
            const auto context =
                application_launch_context(
                    m_app);

            g_desktop_app_info_launch_action(
                desktop_app,
                action,
                context
                    ? G_APP_LAUNCH_CONTEXT(
                          context->gobj())
                    : nullptr);

            g_message(
                "Launched desktop action '%s' for %s",
                action,
                m_app->get_name().c_str());
            return;
        }
    }

    // Applications without a dedicated desktop action conventionally open
    // another window when their normal launcher is activated again.
    launch_application();
}

void DockItem::log_context_action(
    const char *action) const
{
    g_message(
        "Dock context menu: %s (%s)",
        action,
        m_app->get_name().c_str());
}

void DockItem::apply_hover_effect()
{
    if (!m_icon_pixbuf)
        return;

    switch (m_hover_effect)
    {
    case DockHoverEffect::standard:
        m_zoom_animation.disconnect();
        m_zoom_frame = 0;
        m_zoom_target_frame = 0;
        m_blur_animation.disconnect();
        m_blur_frame = 0;
        m_blur_target_frame = 0;

        image.set(
            m_hovered && m_hover_pixbuf
                ? m_hover_pixbuf
                : m_icon_pixbuf);
        break;

    case DockHoverEffect::zoom:
        m_blur_animation.disconnect();
        m_blur_frame = 0;
        m_blur_target_frame = 0;

        if (m_zoom_frames.empty())
        {
            image.set(m_icon_pixbuf);
            return;
        }

        m_zoom_target_frame =
            m_hovered
                ? static_cast<int>(
                      m_zoom_frames.size()) -
                      1
                : 0;

        image.set(
            m_zoom_frames[static_cast<std::size_t>(
                m_zoom_frame)]);

        start_zoom_animation();
        break;

    case DockHoverEffect::blur:
        m_zoom_animation.disconnect();
        m_zoom_frame = 0;
        m_zoom_target_frame = 0;

        if (m_blur_frames.empty())
        {
            image.set(m_icon_pixbuf);
            return;
        }

        m_blur_target_frame =
            m_hovered
                ? static_cast<int>(
                      m_blur_frames.size()) -
                      1
                : 0;

        image.set(
            m_blur_frames[static_cast<std::size_t>(
                m_blur_frame)]);

        start_blur_animation();
        break;
    }
}

void DockItem::create_zoom_frames()
{
    m_zoom_animation.disconnect();
    m_zoom_frame = 0;
    m_zoom_target_frame = 0;
    m_zoom_frames =
        DockIconRenderer::create_zoom_frames(
            m_icon_pixbuf,
            m_icon_size);
}

void DockItem::start_zoom_animation()
{
    if (m_zoom_frame ==
            m_zoom_target_frame ||
        m_zoom_animation.connected())
    {
        return;
    }

    m_zoom_animation =
        Glib::signal_timeout().connect(
            sigc::mem_fun(
                *this,
                &DockItem::
                    advance_zoom_animation),
            ZOOM_FRAME_INTERVAL_MS);
}

bool DockItem::advance_zoom_animation()
{
    if (m_hover_effect !=
            DockHoverEffect::zoom ||
        m_zoom_frames.empty())
    {
        return false;
    }

    if (m_zoom_frame <
        m_zoom_target_frame)
    {
        ++m_zoom_frame;
    }
    else if (m_zoom_frame >
             m_zoom_target_frame)
    {
        --m_zoom_frame;
    }

    image.set(
        m_zoom_frames[static_cast<std::size_t>(
            m_zoom_frame)]);

    return m_zoom_frame !=
           m_zoom_target_frame;
}

void DockItem::create_blur_frames()
{
    m_blur_animation.disconnect();
    m_blur_frame = 0;
    m_blur_target_frame = 0;
    m_blur_frames =
        DockIconRenderer::create_blur_frames(
            m_icon_pixbuf,
            m_icon_size);
}

void DockItem::start_blur_animation()
{
    if (m_blur_frame ==
            m_blur_target_frame ||
        m_blur_animation.connected())
    {
        return;
    }

    m_blur_animation =
        Glib::signal_timeout().connect(
            sigc::mem_fun(
                *this,
                &DockItem::
                    advance_blur_animation),
            BLUR_FRAME_INTERVAL_MS);
}

bool DockItem::advance_blur_animation()
{
    if (m_hover_effect !=
            DockHoverEffect::blur ||
        m_blur_frames.empty())
    {
        return false;
    }

    if (m_blur_frame <
        m_blur_target_frame)
    {
        ++m_blur_frame;
    }
    else if (m_blur_frame >
             m_blur_target_frame)
    {
        --m_blur_frame;
    }

    image.set(
        m_blur_frames[static_cast<std::size_t>(
            m_blur_frame)]);

    return m_blur_frame !=
           m_blur_target_frame;
}
