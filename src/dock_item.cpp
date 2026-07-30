#include "dock_item.h"
#include "dock_configuration_manager.h"
#include "dock_layout_metrics.h"
#include "dock_monitor_manager.h"
#include "dock_window.h"
#include "window_registry.h"
#include "config.h"

#include <gio/gdesktopappinfo.h>
#include <giomm/application.h>
#include <glibmm/miscutils.h>
#include <gtk-layer-shell.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{

    constexpr int ZOOM_FRAME_COUNT = 9; // Frames in a complete zoom transition
    constexpr unsigned int ZOOM_FRAME_INTERVAL_MS = 16; // Delay between zoom frames
    constexpr int BLUR_FRAME_COUNT = 9; // Frames in a complete blur transition
    constexpr unsigned int BLUR_FRAME_INTERVAL_MS = 16; // Delay between blur frames
    constexpr int BLUR_OUTER_RED = 105; // Outer glow red channel
    constexpr int BLUR_OUTER_GREEN = 170; // Outer glow green channel
    constexpr int BLUR_OUTER_BLUE = 255; // Outer glow blue channel
    constexpr int BLUR_INNER_RED = 235; // Inner glow red channel
    constexpr int BLUR_INNER_GREEN = 245; // Inner glow green channel
    constexpr int BLUR_INNER_BLUE = 255; // Inner glow blue channel
    constexpr int BLUR_OUTER_MAX_ALPHA = 225; // Maximum outer glow opacity
    constexpr int BLUR_INNER_MAX_ALPHA = 190; // Maximum inner glow opacity
    constexpr int CONTEXT_MENU_ICON_SIZE = 20; // Window icon size in menu rows
    constexpr int CONTEXT_MENU_TITLE_WIDTH = 48; // Maximum menu title width in characters
    constexpr double INDICATOR_THICKNESS = 2.0;  // Line height
    constexpr double INDICATOR_LINE_INSET = 1.0; // Shortens 1 px on each side
    constexpr double INDICATOR_DOT_RADIUS = 2.0; // Dot radius in pixels
    constexpr double INDICATOR_DOT_GAP = 6.0; // Space between paired dots
    constexpr double INDICATOR_PI = 3.14159265358979323846; // Circle angle calculation

    void keep_dialog_above(
        Gtk::Window &dialog,
        Gtk::Window &parent,
        const char *name_space)
    {
        dialog.set_keep_above(true);

        if (!gtk_layer_is_supported())
            return;

        auto *window =
            GTK_WINDOW(dialog.gobj());

        gtk_layer_init_for_window(window);
        gtk_layer_set_namespace(
            window,
            name_space);
        gtk_layer_set_layer(
            window,
            GTK_LAYER_SHELL_LAYER_OVERLAY);
        gtk_layer_set_exclusive_zone(
            window,
            0);
        gtk_layer_set_keyboard_mode(
            window,
            GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

        const auto parent_window =
            parent.get_window();

        if (!parent_window)
            return;

        auto *display =
            gdk_window_get_display(
                parent_window->gobj());
        auto *monitor =
            gdk_display_get_monitor_at_window(
                display,
                parent_window->gobj());

        gtk_layer_set_monitor(
            window,
            monitor);
    }

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
        const Glib::RefPtr<Gio::AppInfo> &app)
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

    Glib::RefPtr<Gdk::Pixbuf>
    create_transparent_pixbuf(
        int width,
        int height)
    {
        auto pixbuf =
            Gdk::Pixbuf::create(
                Gdk::COLORSPACE_RGB,
                true,
                8,
                std::max(1, width),
                std::max(1, height));

        pixbuf->fill(0x00000000);

        return pixbuf;
    }

    std::vector<float>
    box_blur_alpha(
        const std::vector<float> &source,
        int width,
        int height,
        int radius)
    {
        if (radius <= 0)
            return source;

        const int diameter =
            radius * 2 + 1;

        std::vector<float> horizontal(
            source.size(),
            0.0F);

        std::vector<float> result(
            source.size(),
            0.0F);

        for (int y = 0; y < height; ++y)
        {
            float sum = 0.0F;

            for (int offset = -radius;
                 offset <= radius;
                 ++offset)
            {
                if (offset >= 0 &&
                    offset < width)
                {
                    sum +=
                        source[static_cast<std::size_t>(
                            y * width + offset)];
                }
            }

            for (int x = 0; x < width; ++x)
            {
                horizontal[static_cast<std::size_t>(
                    y * width + x)] =
                    sum / diameter;

                const int remove_x =
                    x - radius;

                const int add_x =
                    x + radius + 1;

                if (remove_x >= 0)
                {
                    sum -=
                        source[static_cast<std::size_t>(
                            y * width +
                            remove_x)];
                }

                if (add_x < width)
                {
                    sum +=
                        source[static_cast<std::size_t>(
                            y * width +
                            add_x)];
                }
            }
        }

        for (int x = 0; x < width; ++x)
        {
            float sum = 0.0F;

            for (int offset = -radius;
                 offset <= radius;
                 ++offset)
            {
                if (offset >= 0 &&
                    offset < height)
                {
                    sum +=
                        horizontal[static_cast<std::size_t>(
                            offset * width + x)];
                }
            }

            for (int y = 0; y < height; ++y)
            {
                result[static_cast<std::size_t>(
                    y * width + x)] =
                    sum / diameter;

                const int remove_y =
                    y - radius;

                const int add_y =
                    y + radius + 1;

                if (remove_y >= 0)
                {
                    sum -=
                        horizontal[static_cast<std::size_t>(
                            remove_y * width +
                            x)];
                }

                if (add_y < height)
                {
                    sum +=
                        horizontal[static_cast<std::size_t>(
                            add_y * width +
                            x)];
                }
            }
        }

        return result;
    }

    std::vector<float>
    blur_alpha(
        std::vector<float> alpha,
        int width,
        int height,
        int radius)
    {
        for (int pass = 0; pass < 3; ++pass)
        {
            alpha =
                box_blur_alpha(
                    alpha,
                    width,
                    height,
                    radius);
        }

        return alpha;
    }

    Glib::RefPtr<Gdk::Pixbuf>
    create_blur_pixbuf(
        const std::vector<float> &alpha,
        int size,
        int red,
        int green,
        int blue)
    {
        auto blur =
            create_transparent_pixbuf(
                size,
                size);

        auto *pixels =
            blur->get_pixels();

        const int rowstride =
            blur->get_rowstride();

        for (int y = 0; y < size; ++y)
        {
            auto *row =
                pixels + y * rowstride;

            for (int x = 0; x < size; ++x)
            {
                auto *pixel =
                    row + x * 4;

                pixel[0] =
                    static_cast<guchar>(red);
                pixel[1] =
                    static_cast<guchar>(green);
                pixel[2] =
                    static_cast<guchar>(blue);
                pixel[3] =
                    static_cast<guchar>(
                        std::clamp(
                            alpha[static_cast<std::size_t>(
                                y * size + x)],
                            0.0F,
                            255.0F));
            }
        }

        return blur;
    }

}

DockItem::DockItem(
    DockWindow &dock,
    Glib::RefPtr<Gio::AppInfo> app,
    WindowRegistry *window_registry,
    int icon_size,
    DockHoverEffect hover_effect,
    DockIndicator indicator,
    const std::string
        &indicator_color)
    : m_dock(dock),
      m_app(app),
      m_application_controller(
          window_registry,
          application_identifiers(app)),
      m_hover_effect(hover_effect),
      m_indicator(indicator),
      m_single_main_window(
          has_single_main_window(app))
{
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
        Gdk::SCROLL_MASK |
        Gdk::SMOOTH_SCROLL_MASK);

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

    initialize_context_menu();
    set_icon_size(icon_size);
    refresh_indicator();

    show_all_children();
}

DockItem::~DockItem()
{
    m_zoom_animation.disconnect();
    m_blur_animation.disconnect();
    m_window_action_idle.disconnect();
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
            create_standard_hover_pixbuf(
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

bool DockItem::on_enter_notify_event(
    GdkEventCrossing *)
{
    m_application_controller
        .reset_window_cycle();
    m_scroll_delta_y = 0.0;

    m_hovered = true;
    apply_hover_effect();

    m_dock.schedule_show_tooltip(*this);

    return true;
}

bool DockItem::on_leave_notify_event(
    GdkEventCrossing *)
{
    m_hovered = false;
    apply_hover_effect();

    m_dock.schedule_hide_tooltip();

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
    if (event->button == GDK_BUTTON_SECONDARY)
    {
        show_context_menu(
            reinterpret_cast<GdkEvent *>(event));
    }
    else if (event->button == GDK_BUTTON_PRIMARY)
    {
        if (!m_application_controller
                 .running())
        {
            launch_application();
        }
        else
        {
            m_application_controller
                .toggle_minimized();
        }
    }

    return true;
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
           unsigned int mnemonic_index)
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

        underline.set_start_index(
            mnemonic_index);
        underline.set_end_index(
            mnemonic_index + 1);
        attributes.insert(underline);
        label->set_attributes(attributes);
    };

    initialize_item(m_attach_item, 1);
    initialize_item(m_open_new_window_item, 0);
    initialize_item(m_minimize_item, 1);
    initialize_item(m_unminimize_item, 0);
    initialize_item(m_maximize_item, 1);
    initialize_item(m_close_all_item, 0);

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
        .signal_activate()
        .connect(
            [this]()
            {
                log_context_action(
                    "Attach");
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

                g_message(
                    "Close all windows for %s: %s",
                    m_app->get_id().c_str(),
                    accepted
                        ? "accepted"
                        : "rejected");
            });

    m_minimize_item
        .signal_activate()
        .connect(
            [this]()
            {
                const bool accepted =
                    m_application_controller
                        .minimize();

                g_message(
                    "Minimize windows for %s: %s",
                    m_app->get_id().c_str(),
                    accepted
                        ? "accepted"
                        : "rejected");
            });

    m_maximize_item
        .signal_activate()
        .connect(
            [this]()
            {
                const bool accepted =
                    m_application_controller
                        .maximize();

                g_message(
                    "Maximize window for %s: %s",
                    m_app->get_id().c_str(),
                    accepted
                        ? "accepted"
                        : "rejected");
            });

    m_unminimize_item
        .signal_activate()
        .connect(
            [this]()
            {
                const bool accepted =
                    m_application_controller
                        .unminimize();

                g_message(
                    "Unminimize windows for %s: %s",
                    m_app->get_id().c_str(),
                    accepted
                        ? "accepted"
                        : "rejected");
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
}

void DockItem::rebuild_window_menu_items()
{
    for (const auto &item :
         m_window_menu_items)
    {
        m_context_menu.remove(*item);
    }

    m_window_menu_items.clear();

    const auto entries =
        m_application_controller
            .window_entries();

    int position = 0;

    for (const auto &entry :
         entries)
    {
        auto item =
            std::make_unique<
                Gtk::MenuItem>();

        item->set_halign(
            Gtk::ALIGN_FILL);
        item->set_valign(
            Gtk::ALIGN_CENTER);

        auto row =
            Gtk::manage(
                new Gtk::Box(
                    Gtk::ORIENTATION_HORIZONTAL,
                    8));

        row->set_halign(
            Gtk::ALIGN_FILL);
        row->set_valign(
            Gtk::ALIGN_CENTER);

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
            context_menu_window_icon(
                entry.icon_name);

        if (pixbuf)
            icon->set(pixbuf);

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
        window_label->set_xalign(0.0F);
        window_label->set_yalign(0.5F);
        window_label->set_ellipsize(
            Pango::ELLIPSIZE_END);
        window_label->set_max_width_chars(
            CONTEXT_MENU_TITLE_WIDTH);

        row->pack_start(
            *icon,
            false,
            false);

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

                g_message(
                    "%s window %s for %s: %s",
                    minimize
                        ? "Minimize"
                        : "Show",
                    window_id.c_str(),
                    m_app->get_id().c_str(),
                    accepted
                        ? "accepted"
                        : "rejected");

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

void DockItem::launch_application()
{
    try
    {
        std::vector<
            Glib::RefPtr<Gio::File>>
            files;

        m_app->launch(files);
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
            g_desktop_app_info_launch_action(
                desktop_app,
                action,
                nullptr);

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
    m_zoom_frames.clear();
    m_zoom_frame = 0;
    m_zoom_target_frame = 0;

    if (!m_icon_pixbuf)
        return;

    const int item_size =
        DockLayoutMetrics::item_size_for(
            m_icon_size);

    const int source_width =
        m_icon_pixbuf->get_width();

    const int source_height =
        m_icon_pixbuf->get_height();

    const int source_extent =
        std::max(
            source_width,
            source_height);

    const int zoom_percent =
        std::min(
            DockLayoutMetrics::
                HOVER_ZOOM_PERCENT,
            item_size * 100 /
                std::max(1, source_extent));

    const int target_width =
        std::max(
            source_width,
            source_width * zoom_percent /
                100);

    const int target_height =
        std::max(
            source_height,
            source_height * zoom_percent /
                100);

    m_zoom_frames.reserve(
        ZOOM_FRAME_COUNT);

    for (int frame_index = 0;
         frame_index < ZOOM_FRAME_COUNT;
         ++frame_index)
    {
        const int width =
            source_width +
            (target_width - source_width) *
                frame_index /
                (ZOOM_FRAME_COUNT - 1);

        const int height =
            source_height +
            (target_height - source_height) *
                frame_index /
                (ZOOM_FRAME_COUNT - 1);

        const int icon_x =
            (item_size - width) / 2;

        const int icon_y =
            (item_size - height) / 2;

        auto frame =
            create_transparent_pixbuf(
                item_size,
                item_size);

        m_icon_pixbuf->composite(
            frame,
            icon_x,
            icon_y,
            width,
            height,
            icon_x,
            icon_y,
            static_cast<double>(width) /
                source_width,
            static_cast<double>(height) /
                source_height,
            Gdk::INTERP_BILINEAR,
            255);

        m_zoom_frames.push_back(
            frame);
    }
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
    m_blur_frames.clear();
    m_blur_frame = 0;
    m_blur_target_frame = 0;

    if (!m_icon_pixbuf)
        return;

    const int item_size =
        DockLayoutMetrics::item_size_for(
            m_icon_size);

    const int icon_x =
        (item_size -
         m_icon_pixbuf->get_width()) /
        2;

    const int icon_y =
        (item_size -
         m_icon_pixbuf->get_height()) /
        2;

    std::vector<float> icon_alpha(
        static_cast<std::size_t>(
            item_size * item_size),
        0.0F);

    const auto *icon_pixels =
        m_icon_pixbuf->get_pixels();

    const int icon_width =
        m_icon_pixbuf->get_width();

    const int icon_height =
        m_icon_pixbuf->get_height();

    const int icon_rowstride =
        m_icon_pixbuf->get_rowstride();

    const int icon_channels =
        m_icon_pixbuf->get_n_channels();

    const bool icon_has_alpha =
        m_icon_pixbuf->get_has_alpha();

    for (int y = 0; y < icon_height; ++y)
    {
        const auto *row =
            icon_pixels +
            y * icon_rowstride;

        for (int x = 0; x < icon_width; ++x)
        {
            const auto *pixel =
                row + x * icon_channels;

            icon_alpha[static_cast<std::size_t>(
                (icon_y + y) *
                    item_size +
                icon_x + x)] =
                icon_has_alpha
                    ? pixel[icon_channels - 1]
                    : 255.0F;
        }
    }

    const int outer_radius =
        std::max(
            2,
            item_size / 24);

    const int inner_radius =
        std::max(
            1,
            item_size / 52);

    const auto outer_alpha =
        blur_alpha(
            icon_alpha,
            item_size,
            item_size,
            outer_radius);

    const auto inner_alpha =
        blur_alpha(
            icon_alpha,
            item_size,
            item_size,
            inner_radius);

    const auto outer_blur =
        create_blur_pixbuf(
            outer_alpha,
            item_size,
            BLUR_OUTER_RED,
            BLUR_OUTER_GREEN,
            BLUR_OUTER_BLUE);

    const auto inner_blur =
        create_blur_pixbuf(
            inner_alpha,
            item_size,
            BLUR_INNER_RED,
            BLUR_INNER_GREEN,
            BLUR_INNER_BLUE);

    m_blur_frames.reserve(
        BLUR_FRAME_COUNT);

    for (int frame_index = 0;
         frame_index < BLUR_FRAME_COUNT;
         ++frame_index)
    {
        auto frame =
            create_transparent_pixbuf(
                item_size,
                item_size);

        const int outer_opacity =
            BLUR_OUTER_MAX_ALPHA *
            frame_index /
            (BLUR_FRAME_COUNT - 1);

        const int inner_opacity =
            BLUR_INNER_MAX_ALPHA *
            frame_index /
            (BLUR_FRAME_COUNT - 1);

        if (outer_opacity > 0)
        {
            outer_blur->composite(
                frame,
                0,
                0,
                item_size,
                item_size,
                0,
                0,
                1.0,
                1.0,
                Gdk::INTERP_BILINEAR,
                outer_opacity);

            inner_blur->composite(
                frame,
                0,
                0,
                item_size,
                item_size,
                0,
                0,
                1.0,
                1.0,
                Gdk::INTERP_BILINEAR,
                inner_opacity);
        }

        m_icon_pixbuf->composite(
            frame,
            icon_x,
            icon_y,
            m_icon_pixbuf->get_width(),
            m_icon_pixbuf->get_height(),
            icon_x,
            icon_y,
            1.0,
            1.0,
            Gdk::INTERP_NEAREST,
            255);

        m_blur_frames.push_back(
            frame);
    }
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

Glib::RefPtr<Gdk::Pixbuf>
DockItem::create_standard_hover_pixbuf(
    const Glib::RefPtr<Gdk::Pixbuf>
        &source) const
{
    if (!source)
        return {};

    auto highlighted =
        source->copy();

    auto *pixels =
        highlighted->get_pixels();

    const int width =
        highlighted->get_width();

    const int height =
        highlighted->get_height();

    const int rowstride =
        highlighted->get_rowstride();

    const int channels =
        highlighted->get_n_channels();

    for (int y = 0;
         y < height;
         ++y)
    {
        auto *row =
            pixels + y * rowstride;

        for (int x = 0;
             x < width;
             ++x)
        {
            auto *pixel =
                row + x * channels;

            for (int channel = 0;
                 channel < 3;
                 ++channel)
            {
                pixel[channel] =
                    static_cast<guchar>(
                        std::min(
                            255,
                            static_cast<int>(
                                pixel[channel]) *
                                    5 /
                                    4 +
                                24));
            }
        }
    }

    return highlighted;
}

DockHomeItem::DockHomeItem(
    DockWindow &dock,
    WindowRegistry *window_registry,
    int icon_size,
    const std::string &icon_path)
    : m_dock(dock),
      m_window_registry(window_registry),
      m_icon_path(icon_path)
{
    set_visible_window(false);

    add_events(
        Gdk::BUTTON_PRESS_MASK);

    m_image.set_halign(
        Gtk::ALIGN_CENTER);
    m_image.set_valign(
        Gtk::ALIGN_CENTER);
    add(m_image);

    signal_popup_menu().connect(
        sigc::mem_fun(
            *this,
            &DockHomeItem::on_popup_menu));

    load_icon_once();
    initialize_context_menu();
    set_icon_size(icon_size);

    show_all_children();
}

DockHomeItem::~DockHomeItem()
{
    m_settings_idle.disconnect();
}

void DockHomeItem::set_icon_size(
    int icon_size)
{
    icon_size = std::max(1, icon_size);

    if (icon_size == m_icon_size)
        return;

    m_icon_size = icon_size;
    update_icon();

    set_size_request(
        DockLayoutMetrics::item_size_for(
            icon_size),
        DockLayoutMetrics::item_size_for(
            icon_size));
}

void DockHomeItem::set_icon_path(
    const std::string &icon_path)
{
    if (icon_path == m_icon_path)
        return;

    m_icon_path = icon_path;
    m_icon_load_attempted = false;
    m_source_icon.reset();
    m_display_icon.reset();
    m_image.clear();

    load_icon_once();
    update_icon();
}

void DockHomeItem::
    set_context_menu_corner_radius(
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

bool DockHomeItem::on_button_press_event(
    GdkEventButton *event)
{
    if (event &&
        event->button ==
            GDK_BUTTON_SECONDARY)
    {
        show_context_menu(
            reinterpret_cast<GdkEvent *>(
                event));
        return true;
    }

    return false;
}

bool DockHomeItem::on_popup_menu()
{
    show_context_menu(nullptr);
    return true;
}

void DockHomeItem::load_icon_once()
{
    if (m_icon_load_attempted)
        return;

    m_icon_load_attempted = true;

    std::vector<std::string> icon_paths;

    if (!m_icon_path.empty())
        icon_paths.push_back(m_icon_path);

    const std::vector<std::string>
        default_icon_paths = {
            Glib::build_filename(
                DOCKLIGHT_DATA_DIR,
                "icons",
                "docklight.home.png"),
            Glib::build_filename(
                SOURCE_DIR,
                "..",
                "data",
                "icons",
                "docklight.home.png"),
            Glib::build_filename(
                SOURCE_DIR,
                "..",
                "data",
                "icons",
                "128x128",
                "docklight.home.png")};

    icon_paths.insert(
        icon_paths.end(),
        default_icon_paths.begin(),
        default_icon_paths.end());

    for (const auto &icon_path :
         icon_paths)
    {
        try
        {
            m_source_icon =
                Gdk::Pixbuf::
                    create_from_file(
                        icon_path);

            if (m_source_icon)
            {
                g_message(
                    "Home icon loaded: %s",
                    icon_path.c_str());
                return;
            }
        }
        catch (const Glib::Error &)
        {
        }
    }

    g_warning(
        "Cannot load DockLight home icon");
}

void DockHomeItem::update_icon()
{
    if (!m_source_icon)
        return;

    if (m_source_icon->get_width() ==
            m_icon_size &&
        m_source_icon->get_height() ==
            m_icon_size)
    {
        m_display_icon = m_source_icon;
    }
    else
    {
        m_display_icon =
            m_source_icon->scale_simple(
                m_icon_size,
                m_icon_size,
                Gdk::INTERP_BILINEAR);
    }

    if (m_display_icon)
        m_image.set(m_display_icon);
}

void DockHomeItem::
    initialize_context_menu()
{
    const auto initialize_mnemonic =
        [](Gtk::MenuItem &item,
           unsigned int mnemonic_index)
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

        Pango::AttrList attributes;
        auto underline =
            Pango::Attribute::
                create_attr_underline(
                    Pango::UNDERLINE_SINGLE);

        underline.set_start_index(
            mnemonic_index);
        underline.set_end_index(
            mnemonic_index + 1);
        attributes.insert(underline);
        label->set_attributes(attributes);
    };

    initialize_mnemonic(
        m_settings_item,
        0);
    initialize_mnemonic(
        m_minimize_all_item,
        0);
    initialize_mnemonic(
        m_unminimize_all_item,
        0);
    initialize_mnemonic(
        m_maximize_all_item,
        2);
    initialize_mnemonic(
        m_close_all_item,
        0);
    initialize_mnemonic(
        m_about_item,
        1);
    initialize_mnemonic(
        m_exit_item,
        0);

    m_context_menu.append(
        m_settings_item);
    m_context_menu.append(
        m_window_separator);
    m_context_menu.append(
        m_minimize_all_item);
    m_context_menu.append(
        m_unminimize_all_item);
    m_context_menu.append(
        m_maximize_all_item);
    m_context_menu.append(
        m_close_separator);
    m_context_menu.append(
        m_close_all_item);
    m_context_menu.append(
        m_about_separator);
    m_context_menu.append(
        m_about_item);
    m_context_menu.append(
        m_exit_separator);
    m_context_menu.append(
        m_exit_item);

    m_settings_item
        .signal_activate()
        .connect(
            [this]()
            {
                m_settings_idle.disconnect();

                m_settings_idle =
                    Glib::signal_idle().connect(
                        [this]()
                        {
                            open_settings();
                            return false;
                        });
            });

    m_minimize_all_item
        .signal_activate()
        .connect(
            [this]()
            {
                minimize_all();
            });

    m_unminimize_all_item
        .signal_activate()
        .connect(
            [this]()
            {
                unminimize_all();
            });

    m_maximize_all_item
        .signal_activate()
        .connect(
            [this]()
            {
                maximize_all();
            });

    m_close_all_item
        .signal_activate()
        .connect(
            [this]()
            {
                close_all();
            });

    m_about_item
        .signal_activate()
        .connect(
            sigc::mem_fun(
                *this,
                &DockHomeItem::show_about));

    m_exit_item
        .signal_activate()
        .connect(
            sigc::mem_fun(
                *this,
                &DockHomeItem::exit_docklight));

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
}

void DockHomeItem::refresh_context_menu()
{
    bool has_window = false;
    bool has_unminimized_window = false;
    bool has_minimized_window = false;
    bool has_unmaximized_window = false;

    WindowBackendCapabilities capabilities;

    if (m_window_registry)
    {
        capabilities =
            m_window_registry->capabilities();

        for (const auto &window :
             m_window_registry->windows())
        {
            has_window = true;
            has_unminimized_window =
                has_unminimized_window ||
                !window.minimized;
            has_minimized_window =
                has_minimized_window ||
                window.minimized;
            has_unmaximized_window =
                has_unmaximized_window ||
                !window.maximized ||
                window.minimized;
        }
    }

    m_minimize_all_item.set_sensitive(
        capabilities.can_minimize &&
        has_unminimized_window);
    m_unminimize_all_item.set_sensitive(
        capabilities.can_minimize &&
        has_minimized_window);
    m_maximize_all_item.set_sensitive(
        capabilities.can_maximize &&
        has_unmaximized_window);
    m_close_all_item.set_sensitive(
        capabilities.can_close &&
        has_window);
}

void DockHomeItem::show_context_menu(
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
        widget_anchor =
            Gdk::GRAVITY_NORTH;
        menu_anchor =
            Gdk::GRAVITY_SOUTH;
        break;

    case DockLocation::top:
        widget_anchor =
            Gdk::GRAVITY_SOUTH;
        menu_anchor =
            Gdk::GRAVITY_NORTH;
        break;

    case DockLocation::left:
        widget_anchor =
            Gdk::GRAVITY_EAST;
        menu_anchor =
            Gdk::GRAVITY_WEST;
        break;

    case DockLocation::right:
        widget_anchor =
            Gdk::GRAVITY_WEST;
        menu_anchor =
            Gdk::GRAVITY_EAST;
        break;
    }

    m_dock.hide_tooltip_immediately();

    m_context_menu.popup_at_widget(
        this,
        widget_anchor,
        menu_anchor,
        event);

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

bool DockHomeItem::minimize_all()
{
    return m_window_registry &&
           m_window_registry
               ->minimize_all();
}

bool DockHomeItem::unminimize_all()
{
    return m_window_registry &&
           m_window_registry
               ->unminimize_all();
}

bool DockHomeItem::maximize_all()
{
    return m_window_registry &&
           m_window_registry
               ->maximize_all();
}

bool DockHomeItem::close_all()
{
    return m_window_registry &&
           m_window_registry
               ->close_all();
}

void DockHomeItem::open_settings()
{
    DockConfigurationManager configuration;
    const auto current =
        configuration.current();

    Gtk::Dialog dialog(
        "DockLight Settings",
        m_dock,
        true);

    dialog.set_type_hint(
        Gdk::WINDOW_TYPE_HINT_DIALOG);
    keep_dialog_above(
        dialog,
        m_dock,
        "docklight6-settings");
    dialog.set_decorated(true);
    dialog.set_resizable(false);
    dialog.property_destroy_with_parent() =
        true;
    dialog.set_skip_taskbar_hint(true);
    dialog.set_skip_pager_hint(true);
    dialog.set_position(
        Gtk::WIN_POS_CENTER_ON_PARENT);
    dialog.set_default_size(460, -1);
    dialog.set_size_request(460, -1);

    Gtk::HeaderBar header;
    Gtk::Image header_icon;

    header.set_title(
        "DockLight Settings");
    header.set_show_close_button(true);
    header.set_decoration_layout(
        ":close");

    if (m_source_icon)
    {
        dialog.set_icon(
            m_source_icon);

        const auto small_home_icon =
            m_source_icon->scale_simple(
                20,
                20,
                Gdk::INTERP_BILINEAR);

        if (small_home_icon)
        {
            header_icon.set(
                small_home_icon);
            header.pack_start(
                header_icon);
        }
    }

    dialog.set_titlebar(header);

    dialog.add_button(
        "_Close",
        Gtk::RESPONSE_CLOSE);

    Gtk::Grid grid;
    grid.set_hexpand(true);
    grid.set_vexpand(false);
    grid.set_border_width(14);
    grid.set_row_spacing(10);
    grid.set_column_spacing(16);
    grid.set_column_homogeneous(false);

    Gtk::Label monitor_label(
        "Monitor");
    Gtk::Label hover_label(
        "Hover effect");
    Gtk::Label indicator_label(
        "Indicator");
    Gtk::Label indicator_color_label(
        "Indicator color");
    Gtk::Label home_icon_enabled_label(
        "Display home icon");
    Gtk::Label home_icon_path_label(
        "Home icon");
    Gtk::Label display_tooltips_label(
        "Display tooltips");
    Gtk::Label icon_size_label(
        "Icon size");
    Gtk::Label location_label(
        "Location");
    Gtk::Label rounded_corners_label(
        "Rounded corners");
    Gtk::Label corner_radius_label(
        "Corner radius");
    Gtk::Label alignment_label(
        "Alignment");
    Gtk::Label autohide_label(
        "Autohide");

    const std::vector<Gtk::Label *>
        labels = {
            &monitor_label,
            &hover_label,
            &indicator_label,
            &indicator_color_label,
            &home_icon_enabled_label,
            &home_icon_path_label,
            &display_tooltips_label,
            &icon_size_label,
            &location_label,
            &rounded_corners_label,
            &corner_radius_label,
            &alignment_label,
            &autohide_label};

    for (auto *field_label : labels)
    {
        field_label->set_halign(
            Gtk::ALIGN_START);
        field_label->set_valign(
            Gtk::ALIGN_CENTER);
    }

    std::vector<std::string>
        monitor_identifiers{"primary"};

    DockMonitorManager monitor_manager;

    for (const auto &monitor :
         monitor_manager.available_monitors())
    {
        if (!monitor.identifier.empty() &&
            std::find(
                monitor_identifiers.begin(),
                monitor_identifiers.end(),
                monitor.identifier) ==
                monitor_identifiers.end())
        {
            monitor_identifiers.push_back(
                monitor.identifier);
        }
    }

    Gtk::ScrolledWindow monitor_scroller;
    Gtk::ListBox monitor_list;

    monitor_scroller.set_policy(
        Gtk::POLICY_NEVER,
        Gtk::POLICY_AUTOMATIC);
    monitor_scroller.set_shadow_type(
        Gtk::SHADOW_IN);
    monitor_scroller.set_size_request(
        -1,
        std::min(
            130,
            34 * static_cast<int>(
                     monitor_identifiers
                         .size())));
    monitor_list.set_selection_mode(
        Gtk::SELECTION_SINGLE);
    monitor_list.set_activate_on_single_click(
        true);

    Gtk::ListBoxRow *selected_monitor_row =
        nullptr;

    for (const auto &identifier :
         monitor_identifiers)
    {
        auto *row =
            Gtk::manage(
                new Gtk::ListBoxRow());

        auto *name =
            Gtk::manage(
                new Gtk::Label(
                    identifier));

        name->set_halign(
            Gtk::ALIGN_START);
        name->set_margin_start(8);
        name->set_margin_end(8);
        name->set_margin_top(5);
        name->set_margin_bottom(5);

        row->add(*name);
        monitor_list.append(*row);

        if (identifier ==
            current.settings.monitor())
        {
            selected_monitor_row = row;
        }
    }

    monitor_scroller.add(
        monitor_list);

    if (!selected_monitor_row)
    {
        selected_monitor_row =
            monitor_list.get_row_at_index(0);
    }

    if (selected_monitor_row)
    {
        monitor_list.select_row(
            *selected_monitor_row);
    }

    Gtk::Box hover_choices(
        Gtk::ORIENTATION_HORIZONTAL,
        6);
    Gtk::RadioButton hover_standard(
        "Standard");
    Gtk::RadioButton hover_zoom(
        "Zoom");
    Gtk::RadioButton hover_blur(
        "Blur");

    hover_zoom.join_group(
        hover_standard);
    hover_blur.join_group(
        hover_standard);

    hover_choices.pack_start(
        hover_standard,
        false,
        false);
    hover_choices.pack_start(
        hover_zoom,
        false,
        false);
    hover_choices.pack_start(
        hover_blur,
        false,
        false);

    switch (current.settings.hover_effect())
    {
    case DockHoverEffect::standard:
        hover_standard.set_active(true);
        break;
    case DockHoverEffect::zoom:
        hover_zoom.set_active(true);
        break;
    case DockHoverEffect::blur:
        hover_blur.set_active(true);
        break;
    }

    Gtk::Box indicator_choices(
        Gtk::ORIENTATION_HORIZONTAL,
        6);
    Gtk::RadioButton indicator_lines(
        "Lines");
    Gtk::RadioButton indicator_dots(
        "Dots");

    indicator_dots.join_group(
        indicator_lines);

    indicator_choices.pack_start(
        indicator_lines,
        false,
        false);
    indicator_choices.pack_start(
        indicator_dots,
        false,
        false);

    if (current.settings.indicator() ==
        DockIndicator::dots)
    {
        indicator_dots.set_active(true);
    }
    else
    {
        indicator_lines.set_active(true);
    }

    Gtk::Button indicator_color;
    Gtk::DrawingArea indicator_color_preview;
    Gdk::RGBA parsed_indicator_color;

    if (!parsed_indicator_color.set(
            current.settings
                .indicator_color()))
    {
        parsed_indicator_color.set(
            "#69aaff");
    }

    indicator_color_preview.set_size_request(
        96,
        24);
    indicator_color_preview
        .signal_draw()
        .connect(
            [&parsed_indicator_color](
                const Cairo::RefPtr<
                    Cairo::Context> &context)
            {
                context->set_source_rgba(
                    parsed_indicator_color
                        .get_red(),
                    parsed_indicator_color
                        .get_green(),
                    parsed_indicator_color
                        .get_blue(),
                    parsed_indicator_color
                        .get_alpha());
                context->paint();

                return true;
            });

    indicator_color.add(
        indicator_color_preview);
    indicator_color.set_tooltip_text(
        "Choose indicator color");

    Gtk::CheckButton home_icon_enabled;
    home_icon_enabled.set_active(
        current.settings
            .home_icon_enabled());

    Gtk::Box home_icon_controls(
        Gtk::ORIENTATION_HORIZONTAL,
        6);
    Gtk::Entry home_icon_path;
    Gtk::Button select_home_icon(
        "Select...");
    Gtk::Button use_default_home_icon(
        "Use default");

    const auto configured_home_icon_path =
        current.settings.home_icon_path();

    home_icon_path.set_editable(false);
    home_icon_path.set_hexpand(true);
    home_icon_path.set_placeholder_text(
        "Built-in DockLight icon");
    home_icon_path.set_text(
        configured_home_icon_path);
    home_icon_path.set_tooltip_text(
        configured_home_icon_path);

    home_icon_controls.pack_start(
        home_icon_path,
        true,
        true);
    home_icon_controls.pack_start(
        select_home_icon,
        false,
        false);
    home_icon_controls.pack_start(
        use_default_home_icon,
        false,
        false);

    Gtk::CheckButton display_tooltips;
    display_tooltips.set_active(
        current.settings
            .display_tooltips());

    auto icon_size_adjustment =
        Gtk::Adjustment::create(
            current.settings.icon_size(),
            32.0,
            128.0,
            1.0,
            4.0);

    Gtk::SpinButton icon_size_spin(
        icon_size_adjustment,
        1.0,
        0);
    icon_size_spin.set_numeric(true);

    Gtk::Box location_choices(
        Gtk::ORIENTATION_HORIZONTAL,
        6);
    Gtk::RadioButton location_bottom(
        "Bottom");
    Gtk::RadioButton location_left(
        "Left");
    Gtk::RadioButton location_top(
        "Top");
    Gtk::RadioButton location_right(
        "Right");

    location_left.join_group(
        location_bottom);
    location_top.join_group(
        location_bottom);
    location_right.join_group(
        location_bottom);

    location_choices.pack_start(
        location_bottom,
        false,
        false);
    location_choices.pack_start(
        location_left,
        false,
        false);
    location_choices.pack_start(
        location_top,
        false,
        false);
    location_choices.pack_start(
        location_right,
        false,
        false);

    switch (current.layout_request.location)
    {
    case DockLocation::bottom:
        location_bottom.set_active(true);
        break;
    case DockLocation::left:
        location_left.set_active(true);
        break;
    case DockLocation::top:
        location_top.set_active(true);
        break;
    case DockLocation::right:
        location_right.set_active(true);
        break;
    }

    Gtk::CheckButton rounded_corners;
    rounded_corners.set_active(
        current.layout_request
            .rounded_corners);

    auto corner_radius_adjustment =
        Gtk::Adjustment::create(
            current.layout_request
                .corner_radius,
            -1.0,
            current.settings.icon_size() /
                2.0,
            1.0,
            2.0);

    Gtk::SpinButton corner_radius_spin(
        corner_radius_adjustment,
        1.0,
        0);
    corner_radius_spin.set_numeric(true);
    corner_radius_spin.set_tooltip_text(
        "-1 selects the automatic radius");

    Gtk::Box alignment_choices(
        Gtk::ORIENTATION_HORIZONTAL,
        6);
    Gtk::RadioButton alignment_start(
        "Start");
    Gtk::RadioButton alignment_center(
        "Center");
    Gtk::RadioButton alignment_end(
        "End");
    Gtk::RadioButton alignment_fill(
        "Fill");

    alignment_center.join_group(
        alignment_start);
    alignment_end.join_group(
        alignment_start);
    alignment_fill.join_group(
        alignment_start);

    alignment_choices.pack_start(
        alignment_start,
        false,
        false);
    alignment_choices.pack_start(
        alignment_center,
        false,
        false);
    alignment_choices.pack_start(
        alignment_end,
        false,
        false);
    alignment_choices.pack_start(
        alignment_fill,
        false,
        false);

    switch (current.layout_request.alignment)
    {
    case DockAlignment::start:
        alignment_start.set_active(true);
        break;
    case DockAlignment::center:
        alignment_center.set_active(true);
        break;
    case DockAlignment::end:
        alignment_end.set_active(true);
        break;
    case DockAlignment::fill:
        alignment_fill.set_active(true);
        break;
    }

    Gtk::Box autohide_choices(
        Gtk::ORIENTATION_HORIZONTAL,
        6);
    Gtk::RadioButton autohide_none(
        "None");
    Gtk::RadioButton autohide_always(
        "Autohide");
    Gtk::RadioButton autohide_intelligent(
        "Intellihide");

    autohide_always.join_group(
        autohide_none);
    autohide_intelligent.join_group(
        autohide_none);

    autohide_choices.pack_start(
        autohide_none,
        false,
        false);
    autohide_choices.pack_start(
        autohide_always,
        false,
        false);
    autohide_choices.pack_start(
        autohide_intelligent,
        false,
        false);

    switch (current.layout_request.autohide)
    {
    case DockAutohide::none:
        autohide_none.set_active(true);
        break;
    case DockAutohide::autohide:
        autohide_always.set_active(true);
        break;
    case DockAutohide::intellihide:
        autohide_intelligent
            .set_active(true);
        break;
    }

    const std::vector<Gtk::Widget *>
        fields = {
            &monitor_scroller,
            &hover_choices,
            &indicator_choices,
            &indicator_color,
            &home_icon_enabled,
            &home_icon_controls,
            &display_tooltips,
            &icon_size_spin,
            &location_choices,
            &rounded_corners,
            &corner_radius_spin,
            &alignment_choices,
            &autohide_choices};

    for (auto *field : fields)
    {
        field->set_hexpand(true);
        field->set_halign(
            Gtk::ALIGN_FILL);
        field->set_valign(
            Gtk::ALIGN_CENTER);
    }

    rounded_corners.set_halign(
        Gtk::ALIGN_START);
    home_icon_enabled.set_halign(
        Gtk::ALIGN_START);
    display_tooltips.set_halign(
        Gtk::ALIGN_START);

    grid.attach(
        monitor_label,
        0,
        0,
        1,
        1);
    grid.attach(
        monitor_scroller,
        1,
        0,
        1,
        1);
    grid.attach(
        hover_label,
        0,
        1,
        1,
        1);
    grid.attach(
        hover_choices,
        1,
        1,
        1,
        1);
    grid.attach(
        indicator_label,
        0,
        2,
        1,
        1);
    grid.attach(
        indicator_choices,
        1,
        2,
        1,
        1);
    grid.attach(
        indicator_color_label,
        0,
        3,
        1,
        1);
    grid.attach(
        indicator_color,
        1,
        3,
        1,
        1);
    grid.attach(
        home_icon_enabled_label,
        0,
        4,
        1,
        1);
    grid.attach(
        home_icon_enabled,
        1,
        4,
        1,
        1);
    grid.attach(
        home_icon_path_label,
        0,
        5,
        1,
        1);
    grid.attach(
        home_icon_controls,
        1,
        5,
        1,
        1);
    grid.attach(
        display_tooltips_label,
        0,
        6,
        1,
        1);
    grid.attach(
        display_tooltips,
        1,
        6,
        1,
        1);
    grid.attach(
        icon_size_label,
        0,
        7,
        1,
        1);
    grid.attach(
        icon_size_spin,
        1,
        7,
        1,
        1);
    grid.attach(
        location_label,
        0,
        8,
        1,
        1);
    grid.attach(
        location_choices,
        1,
        8,
        1,
        1);
    grid.attach(
        rounded_corners_label,
        0,
        9,
        1,
        1);
    grid.attach(
        rounded_corners,
        1,
        9,
        1,
        1);
    grid.attach(
        corner_radius_label,
        0,
        10,
        1,
        1);
    grid.attach(
        corner_radius_spin,
        1,
        10,
        1,
        1);
    grid.attach(
        alignment_label,
        0,
        11,
        1,
        1);
    grid.attach(
        alignment_choices,
        1,
        11,
        1,
        1);
    grid.attach(
        autohide_label,
        0,
        12,
        1,
        1);
    grid.attach(
        autohide_choices,
        1,
        12,
        1,
        1);

    monitor_list
        .signal_row_selected()
        .connect(
            [&configuration,
             &monitor_identifiers](
                Gtk::ListBoxRow *row)
            {
                if (!row)
                    return;

                const int index =
                    row->get_index();

                if (index < 0 ||
                    index >=
                        static_cast<int>(
                            monitor_identifiers
                                .size()))
                {
                    return;
                }

                configuration.save_setting(
                    "monitor",
                    monitor_identifiers[
                        static_cast<
                            std::size_t>(
                            index)]);
            });

    const auto connect_radio =
        [&configuration](
            Gtk::RadioButton &button,
            const std::string &key,
            const std::string &value)
    {
        button.signal_toggled().connect(
            [&configuration,
             &button,
             key,
             value]()
            {
                if (button.get_active())
                {
                    configuration.save_setting(
                        key,
                        value);
                }
            });
    };

    connect_radio(
        hover_standard,
        "hover_effect",
        "standard");
    connect_radio(
        hover_zoom,
        "hover_effect",
        "zoom");
    connect_radio(
        hover_blur,
        "hover_effect",
        "blur");
    connect_radio(
        indicator_lines,
        "indicator",
        "lines");
    connect_radio(
        indicator_dots,
        "indicator",
        "dots");
    connect_radio(
        location_bottom,
        "location",
        "bottom");
    connect_radio(
        location_left,
        "location",
        "left");
    connect_radio(
        location_top,
        "location",
        "top");
    connect_radio(
        location_right,
        "location",
        "right");
    connect_radio(
        alignment_start,
        "alignment",
        "start");
    connect_radio(
        alignment_center,
        "alignment",
        "center");
    connect_radio(
        alignment_end,
        "alignment",
        "end");
    connect_radio(
        alignment_fill,
        "alignment",
        "fill");
    connect_radio(
        autohide_none,
        "autohide",
        "none");
    connect_radio(
        autohide_always,
        "autohide",
        "autohide");
    connect_radio(
        autohide_intelligent,
        "autohide",
        "intellihide");

    home_icon_enabled
        .signal_toggled()
        .connect(
            [&configuration,
             &home_icon_enabled]()
            {
                configuration.save_setting(
                    "home_icon_enabled",
                    home_icon_enabled
                            .get_active()
                        ? "true"
                        : "false");
            });

    display_tooltips
        .signal_toggled()
        .connect(
            [&configuration,
             &display_tooltips]()
            {
                configuration.save_setting(
                    "display_tooltips",
                    display_tooltips
                            .get_active()
                        ? "true"
                        : "false");
            });

    select_home_icon
        .signal_clicked()
        .connect(
            [&configuration,
             &dialog,
             &home_icon_path,
             this]()
            {
                Gtk::Dialog
                    icon_dialog(
                        "Select home icon",
                        dialog,
                        true);

                icon_dialog.add_button(
                    "_Cancel",
                    Gtk::RESPONSE_CANCEL);
                icon_dialog.add_button(
                    "_Open",
                    Gtk::RESPONSE_OK);
                icon_dialog.set_type_hint(
                    Gdk::WINDOW_TYPE_HINT_DIALOG);
                keep_dialog_above(
                    icon_dialog,
                    dialog,
                    "docklight6-icon-chooser");
                icon_dialog.set_decorated(true);
                icon_dialog.set_resizable(true);
                icon_dialog
                    .property_destroy_with_parent() =
                    true;
                icon_dialog
                    .set_skip_taskbar_hint(true);
                icon_dialog
                    .set_skip_pager_hint(true);
                icon_dialog.set_position(
                    Gtk::WIN_POS_CENTER_ON_PARENT);
                icon_dialog.set_default_size(
                    760,
                    520);

                Gtk::HeaderBar icon_header;
                Gtk::Image icon_header_icon;

                icon_header.set_title(
                    "Select home icon");
                icon_header
                    .set_show_close_button(true);
                icon_header.set_decoration_layout(
                    ":close");

                if (m_source_icon)
                {
                    icon_dialog.set_icon(
                        m_source_icon);

                    const auto small_home_icon =
                        m_source_icon->scale_simple(
                            20,
                            20,
                            Gdk::INTERP_BILINEAR);

                    if (small_home_icon)
                    {
                        icon_header_icon.set(
                            small_home_icon);
                        icon_header.pack_start(
                            icon_header_icon);
                    }
                }

                icon_dialog.set_titlebar(
                    icon_header);

                Gtk::FileChooserWidget
                    icon_chooser(
                        Gtk::
                            FILE_CHOOSER_ACTION_OPEN);

                auto image_filter =
                    Gtk::FileFilter::create();
                image_filter->set_name(
                    "Image files");
                image_filter
                    ->add_pixbuf_formats();
                icon_chooser.add_filter(
                    image_filter);

                const auto current_path =
                    home_icon_path.get_text();

                if (!current_path.empty())
                {
                    icon_chooser.set_filename(
                        current_path);
                }

                auto *icon_content =
                    icon_dialog.get_content_area();

                icon_content->pack_start(
                    icon_chooser,
                    true,
                    true);

                icon_dialog.show_all_children();
                icon_dialog.present();

                if (icon_dialog.run() ==
                    Gtk::RESPONSE_OK)
                {
                    const auto selected_path =
                        icon_chooser
                            .get_filename();

                    if (!selected_path.empty())
                    {
                        home_icon_path.set_text(
                            selected_path);
                        home_icon_path
                            .set_tooltip_text(
                                selected_path);
                        configuration.save_setting(
                            "home_icon_path",
                            selected_path);
                    }
                }

                icon_dialog.hide();
            });

    use_default_home_icon
        .signal_clicked()
        .connect(
            [&configuration,
             &home_icon_path]()
            {
                home_icon_path.set_text("");
                home_icon_path
                    .set_tooltip_text("");
                configuration.save_setting(
                    "home_icon_path",
                    "");
            });

    indicator_color
        .signal_clicked()
        .connect(
            [&configuration,
             &dialog,
             &indicator_color_preview,
             &parsed_indicator_color,
             this]()
            {
                Gtk::ColorChooserDialog
                    color_dialog(
                        "Indicator color",
                        dialog);

                color_dialog.set_modal(true);
                color_dialog.set_type_hint(
                    Gdk::WINDOW_TYPE_HINT_DIALOG);
                keep_dialog_above(
                    color_dialog,
                    dialog,
                    "docklight6-color-chooser");
                color_dialog.set_decorated(true);
                color_dialog
                    .property_destroy_with_parent() =
                    true;
                color_dialog
                    .set_skip_taskbar_hint(true);
                color_dialog
                    .set_skip_pager_hint(true);
                color_dialog.set_position(
                    Gtk::WIN_POS_CENTER_ON_PARENT);
                color_dialog.set_use_alpha(true);
                color_dialog.set_rgba(
                    parsed_indicator_color);

                Gtk::HeaderBar color_header;
                Gtk::Image color_header_icon;

                color_header.set_title(
                    "Indicator color");
                color_header
                    .set_show_close_button(true);
                color_header.set_decoration_layout(
                    ":close");

                if (m_source_icon)
                {
                    color_dialog.set_icon(
                        m_source_icon);

                    const auto small_home_icon =
                        m_source_icon->scale_simple(
                            20,
                            20,
                            Gdk::INTERP_BILINEAR);

                    if (small_home_icon)
                    {
                        color_header_icon.set(
                            small_home_icon);
                        color_header.pack_start(
                            color_header_icon);
                    }
                }

                color_dialog.set_titlebar(
                    color_header);
                color_dialog.show_all_children();
                color_dialog.present();

                if (color_dialog.run() !=
                    Gtk::RESPONSE_OK)
                {
                    color_dialog.hide();
                    return;
                }

                parsed_indicator_color =
                    color_dialog.get_rgba();
                indicator_color_preview
                    .queue_draw();

                configuration.save_setting(
                    "indicator_color",
                    parsed_indicator_color
                        .to_string());

                color_dialog.hide();
            });

    icon_size_spin
        .signal_value_changed()
        .connect(
            [&configuration,
             &icon_size_spin,
             &corner_radius_spin,
             &corner_radius_adjustment]()
            {
                const int icon_size =
                    icon_size_spin
                        .get_value_as_int();

                configuration.save_setting(
                    "icon_size",
                    std::to_string(
                        icon_size));

                const int maximum_radius =
                    icon_size / 2;

                corner_radius_adjustment
                    ->set_upper(
                        maximum_radius);

                const int corner_radius =
                    corner_radius_spin
                        .get_value_as_int();

                if (corner_radius != -1 &&
                    corner_radius >
                        maximum_radius)
                {
                    corner_radius_spin
                        .set_value(
                            maximum_radius);
                }
            });

    rounded_corners
        .signal_toggled()
        .connect(
            [&configuration,
             &rounded_corners]()
            {
                configuration.save_setting(
                    "rounded_corners",
                    rounded_corners
                            .get_active()
                        ? "true"
                        : "false");
            });

    corner_radius_spin
        .signal_value_changed()
        .connect(
            [&configuration,
             &corner_radius_spin]()
            {
                configuration.save_setting(
                    "corner_radius",
                    std::to_string(
                        corner_radius_spin
                            .get_value_as_int()));
            });

    auto *content =
        dialog.get_content_area();

    content->pack_start(
        grid,
        false,
        false);

    dialog.show_all_children();
    dialog.present();
    dialog.run();
    dialog.hide();
}

void DockHomeItem::show_about()
{
    Gtk::Dialog dialog(
        "About DockLight",
        m_dock,
        true);

    dialog.set_type_hint(
        Gdk::WINDOW_TYPE_HINT_DIALOG);
    keep_dialog_above(
        dialog,
        m_dock,
        "docklight6-about");
    dialog.set_decorated(true);
    dialog.set_resizable(false);
    dialog.property_destroy_with_parent() =
        true;
    dialog.set_skip_taskbar_hint(true);
    dialog.set_skip_pager_hint(true);
    dialog.set_position(
        Gtk::WIN_POS_CENTER_ON_PARENT);
    dialog.set_default_size(
        600,
        -1);
    dialog.set_size_request(
        600,
        -1);

    Gtk::HeaderBar header;
    Gtk::Image header_icon;

    header.set_title(
        "About DockLight");
    header.set_show_close_button(true);
    header.set_decoration_layout(
        ":close");

    if (m_source_icon)
    {
        dialog.set_icon(m_source_icon);

        const auto small_home_icon =
            m_source_icon->scale_simple(
                20,
                20,
                Gdk::INTERP_BILINEAR);

        if (small_home_icon)
        {
            header_icon.set(
                small_home_icon);
            header.pack_start(
                header_icon);
        }
    }

    dialog.set_titlebar(header);

    dialog.add_button(
        "_Close",
        Gtk::RESPONSE_CLOSE);

    Gtk::Box about_content(
        Gtk::ORIENTATION_VERTICAL,
        10);
    Gtk::Image logo;
    Gtk::Label program_name;
    Gtk::Label version(
        std::string("Version ") +
        VERSION);
    Gtk::Label comments(
        "A lightweight application dock.\n"
        "Author/Maintener: yoosamui");
    Gtk::LinkButton website(
        "https://github.com/yoosamui/DockLight",
        "yoosamui/DockLight");

    about_content.set_border_width(20);

    if (m_source_icon)
    {
        const auto logo_pixbuf =
            m_source_icon->scale_simple(
                96,
                96,
                Gdk::INTERP_BILINEAR);

        if (logo_pixbuf)
            logo.set(logo_pixbuf);
    }

    program_name.set_markup(
        "<span size=\"xx-large\" "
        "weight=\"bold\">Docklight 6.0</span>");
    program_name.set_justify(
        Gtk::JUSTIFY_CENTER);
    version.set_justify(
        Gtk::JUSTIFY_CENTER);
    comments.set_justify(
        Gtk::JUSTIFY_CENTER);
    website.set_halign(
        Gtk::ALIGN_CENTER);

    about_content.pack_start(
        logo,
        false,
        false);
    about_content.pack_start(
        program_name,
        false,
        false);
    about_content.pack_start(
        version,
        false,
        false);
    about_content.pack_start(
        comments,
        false,
        false);
    about_content.pack_start(
        website,
        false,
        false);

    dialog.get_content_area()
        ->pack_start(
            about_content,
            true,
            true);

    dialog.show_all_children();
    dialog.present();
    dialog.run();
    dialog.hide();
}

void DockHomeItem::exit_docklight()
{
    auto application =
        Gio::Application::get_default();

    if (application)
        application->quit();
    else
        m_dock.hide();
}
