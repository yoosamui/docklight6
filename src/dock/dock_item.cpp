// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_item.cpp
//
// Implementation overview:
// Implements launcher construction, rendering, event handling, and simple
// application actions.
//
// Important implementation decisions:
// - GTK event handlers coordinate UI state but delegate window policy.
// - Icon geometry is converted to plain data before publication.
// - Menu, effect, and drag methods live in neighboring build units while
//   sharing the same class declaration.
//
// ------------------------------------------------------------

#include "dock_item.h"
#include "dock_constants.h"
#include "rendering/dock_icon_renderer.h"
#include "layout/dock_layout_metrics.h"
#include "dock_window.h"

#include <gio/gdesktopappinfo.h>
#include <glibmm/i18n.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{

    constexpr double INDICATOR_THICKNESS = 2.0;  // Line height
    constexpr double INDICATOR_LINE_INSET = 1.0; // Shortens 1 px on each side
    constexpr double INDICATOR_DOT_RADIUS = 2.0; // Dot radius in pixels
    constexpr double INDICATOR_DOT_GAP = 6.0; // Space between paired dots
    constexpr double INDICATOR_PI = 3.14159265358979323846; // Circle angle calculation


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


} // namespace

// Every identity a window may report for this application: the desktop ID, the
// entry ID, the executable, its themed icon names, and StartupWMClass. Items
// that group several applications merge one of these lists per application.
std::vector<std::string>
DockItem::application_identifiers(
    const Glib::RefPtr<Gio::AppInfo> &app,
    const std::string &desktop_id,
    bool include_icon_names)
{
    std::vector<std::string> identifiers;

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

    if (!app)
        return identifiers;

    add_identifier(app->get_id());
    add_identifier(app->get_executable());

    const auto icon = app->get_icon();

    if (include_icon_names &&
        icon &&
        G_IS_THEMED_ICON(icon->gobj()))
    {
        const auto icon_names =
            g_themed_icon_get_names(
                G_THEMED_ICON(icon->gobj()));

        for (int index = 0;
             icon_names && icon_names[index];
             ++index)
        {
            add_identifier(icon_names[index]);
        }
    }

    if (G_IS_DESKTOP_APP_INFO(app->gobj()))
    {
        const auto startup_wm_class =
            g_desktop_app_info_get_startup_wm_class(
                G_DESKTOP_APP_INFO(app->gobj()));

        if (startup_wm_class)
            add_identifier(startup_wm_class);
    }

    return identifiers;
}

// Shared construction for both constructors: menu labels, indicator colour,
// event mask, and the initial icon size.
void DockItem::initialize(
    int icon_size,
    const std::string &indicator_color)
{
    const auto set_menu_label =
        [](Gtk::MenuItem &item,
           const Glib::ustring &text)
    {
        item.set_label(text);
        item.set_use_underline(true);
    };

    set_menu_label(
        m_edit_item,
        _("_Edit"));
    set_menu_label(
        m_remove_item,
        _("_Remove"));
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

    show_all_children();}

// Delegating constructor for items that are not one installed application.
// The application pointer stays null and the grouped identifiers are supplied
// directly, so DockApplicationController still drives the indicator, the
// previews, and the window actions.
DockItem::DockItem(
    DockWindow &dock,
    const std::string &desktop_id,
    std::vector<std::string>
        application_identifiers,
    WindowRegistry *window_registry,
    int icon_size,
    DockHoverEffect hover_effect,
    DockIndicator indicator,
    const std::string
        &indicator_color)
    : m_dock(dock),
      m_desktop_id(desktop_id),
      m_application_controller(
          window_registry,
          std::move(application_identifiers)),
      m_hover_effect(hover_effect),
      m_indicator(indicator),
      m_attached(true),
      m_single_main_window(false)
{
    initialize(icon_size, indicator_color);
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
    initialize(icon_size, indicator_color);
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
    m_context_menu_uninhibit_idle.disconnect();
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
    // Items that are not backed by an installed application supply their own
    // image by overriding this. The base is also reached from initialize()
    // during construction, before the subclass vtable exists, so it must
    // tolerate a null application rather than dereference it.
    if (!m_app)
        return;

    auto icon = m_app->get_icon();
    auto icon_theme =
        Gtk::IconTheme::get_default();

    if (!icon_theme)
    {
        g_warning(
            "Cannot load icon for %s: no GTK icon theme",
            app_name().c_str());
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
            app_name().c_str());
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
                app_name().c_str());
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

        apply_icon_pixbuf(pixbuf);
    }
    catch (const Glib::Error &error)
    {
        // Keep the previously displayed pixbuf when the new theme contains
        // a broken icon. One bad asset must not leave an empty dock item.
        const auto error_message =
            error.what();

        g_warning(
            "Cannot reload icon for %s: %s",
            app_name().c_str(),
            error_message.c_str());
    }
}

Glib::ustring DockItem::app_name() const
{
    return m_app
               ? Glib::ustring(
                     m_app->get_display_name())
               : Glib::ustring(m_desktop_id);
}

// Applies one already-sized image and rebuilds every pixbuf derived from it:
// the hover image and the zoom or blur animation frames. Subclasses whose
// image does not come from a Gio::AppInfo reuse this so hover effects,
// indicators, and animations behave identically.
void DockItem::set_application_identifiers(
    std::vector<std::string>
        application_identifiers)
{
    m_application_controller
        .set_application_identifiers(
            std::move(application_identifiers));
    refresh_indicator();
}

void DockItem::set_window_filter(
    std::function<bool(const ManagedWindow &)>
        window_filter)
{
    m_application_controller
        .set_window_filter(
            std::move(window_filter));
    refresh_indicator();
}

void DockItem::apply_icon_pixbuf(
    const Glib::RefPtr<Gdk::Pixbuf> &pixbuf)
{
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
