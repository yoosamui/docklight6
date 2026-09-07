// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_home_item.cpp
//
// Implementation overview:
// Implements the dock home widget, its menu, and global dock actions.
//
// Important implementation decisions:
// - Menu actions are routed to dock, settings, about, and window services.
// - Layout metrics determine sizing consistently with other dock items.
// - Registry-dependent actions remain optional when integration is
//   unavailable.
// - Unchanged magnified icon frames are cached per item.
//
// ------------------------------------------------------------

#include "dock_home_item.h"
#include "dialogs/dock_about_dialog.h"
#include "dialogs/dock_session_dialog.h"
#include "dialogs/dock_settings_dialog.h"
#include "dock_constants.h"
#include "layout/dock_layout_metrics.h"
#include "rendering/dock_icon_renderer.h"
#include "dock_window.h"
#include "windowing/window_registry.h"
#include "config.h"

#include <giomm/application.h>
#include <gtkmm/dialog.h>
#include <glibmm/i18n.h>
#include <glibmm/miscutils.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

DockHomeItem::DockHomeItem(
    DockWindow &dock,
    WindowRegistry *window_registry,
    const DockRuntimeInfo &runtime_info,
    int icon_size,
    const std::string &icon_path)
    : m_dock(dock),
      m_window_registry(window_registry),
      m_runtime_info(runtime_info),
      m_icon_path(icon_path)
{
    const auto set_menu_label =
        [](Gtk::MenuItem &item,
           const Glib::ustring &text)
    {
        item.set_label(text);
        item.set_use_underline(true);
    };

    set_menu_label(
        m_settings_item,
        _("_Settings"));
    set_menu_label(
        m_session_item,
        _("Sessi_on"));
    set_menu_label(
        m_minimize_all_item,
        _("_Minimize All"));
    set_menu_label(
        m_unminimize_all_item,
        _("_Unminimize All"));
    set_menu_label(
        m_maximize_all_item,
        _("Ma_ximize All"));
    set_menu_label(
        m_close_all_item,
        _("_Close All"));
    set_menu_label(
        m_about_item,
        _("_About"));
    set_menu_label(
        m_exit_item,
        _("_Exit"));

    set_visible_window(false);

    add_events(
        Gdk::ENTER_NOTIFY_MASK |
        Gdk::LEAVE_NOTIFY_MASK |
        Gdk::POINTER_MOTION_MASK |
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
    m_context_menu_map.disconnect();
    m_context_menu_unmap.disconnect();
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
    if (m_magnified_enabled)
        apply_magnified_size_request();
}

const Glib::RefPtr<Gdk::Pixbuf> &DockHomeItem::source_icon() const
{
    return m_source_icon;
}

void DockHomeItem::set_magnified_scale(double scale)
{
    if (!m_magnified_enabled)
        return;

    scale = std::clamp(scale, 1.0, 2.25);
    // Preserve the interpolator's final steps so release can reach 1.0.
    if (scale == m_magnified_scale)
        return;

    m_magnified_scale = scale;
}

void DockHomeItem::set_magnified_layer_active(
    bool active)
{
    if (!m_magnified_enabled)
        return;

    const bool state_changed =
        m_magnified_layer_active != active;
    m_magnified_layer_active = active;
    if (!active)
        m_image.set(m_display_icon);
    m_image.set_opacity(active ? 0.0 : 1.0);
    if (state_changed)
        queue_draw();
}

Glib::RefPtr<Gdk::Pixbuf> DockHomeItem::magnified_icon(
    double scale) const
{
    // Most icons stay at normal scale during pointer motion. Keep one frame
    // per item instead of allocating and rasterizing every icon on every tick.
    if (m_magnified_cached_source != m_display_icon ||
        m_magnified_cached_scale != scale ||
        m_magnified_cached_size != m_icon_size)
    {
        m_magnified_cached_source = m_display_icon;
        m_magnified_cached_scale = scale;
        m_magnified_cached_size = m_icon_size;
        m_magnified_cached_icon = DockIconRenderer::create_magnified(
            m_display_icon,
            m_icon_size,
            scale);
    }
    return m_magnified_cached_icon;
}

double DockHomeItem::magnified_scale() const
{
    return m_magnified_scale;
}

void DockHomeItem::set_vertical(bool vertical)
{
    if (m_vertical == vertical)
        return;

    m_vertical = vertical;
    if (m_magnified_enabled)
        apply_magnified_size_request();
}

void DockHomeItem::set_magnified_enabled(bool enabled)
{
    m_magnified_enabled = enabled;
    m_magnified_scale = 1.0;
    if (enabled)
    {
        apply_magnified_size_request();
        m_image.set(m_display_icon);
        m_image.set_opacity(1.0);
    }
    else
    {
        m_image.set_opacity(1.0);
        set_size_request(
            DockLayoutMetrics::item_size_for(m_icon_size),
            DockLayoutMetrics::item_size_for(m_icon_size));
        update_icon();
    }
}

void DockHomeItem::apply_magnified_size_request()
{
    const int base_size =
        DockLayoutMetrics::item_size_for(m_icon_size);
    set_size_request(
        base_size,
        base_size);
}

ItemGeometry DockHomeItem::icon_geometry()
{
    const auto allocation = m_image.get_allocation();
    ItemGeometry geometry;
    m_image.translate_coordinates(
        m_dock,
        0,
        0,
        geometry.x,
        geometry.y);
    geometry.width = allocation.get_width();
    geometry.height = allocation.get_height();
    geometry.center_x = geometry.x + geometry.width / 2;
    geometry.center_y = geometry.y + geometry.height / 2;
    return geometry;
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

bool DockHomeItem::on_enter_notify_event(
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

    m_dock.schedule_show_tooltip(
        *this,
        C_("dock tooltip", "Home"));

    return true;
}

bool DockHomeItem::on_leave_notify_event(
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

    m_dock.schedule_hide_tooltip(*this);
    return false;
}

bool DockHomeItem::on_motion_notify_event(
    GdkEventMotion *event)
{
    if (event)
    {
        int x = 0;
        int y = 0;
        translate_coordinates(
            m_dock,
            static_cast<int>(event->x),
            static_cast<int>(event->y),
            x,
            y);
        m_dock.update_magnified_hover(x, y);
    }

    return Gtk::EventBox::on_motion_notify_event(event);
}

bool DockHomeItem::on_button_press_event(
    GdkEventButton *event)
{
    if (!event)
        return false;

    if (event->button ==
        GDK_BUTTON_SECONDARY)
    {
        show_context_menu(
            reinterpret_cast<GdkEvent *>(
                event));
        return true;
    }

    if (event->button ==
        GDK_BUTTON_PRIMARY)
    {
        schedule_open_settings();
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
        [](Gtk::MenuItem &item)
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
        label->set_attributes(attributes);
    };

    initialize_mnemonic(
        m_settings_item);
    initialize_mnemonic(
        m_session_item);
    initialize_mnemonic(
        m_minimize_all_item);
    initialize_mnemonic(
        m_unminimize_all_item);
    initialize_mnemonic(
        m_maximize_all_item);
    initialize_mnemonic(
        m_close_all_item);
    initialize_mnemonic(
        m_about_item);
    initialize_mnemonic(
        m_exit_item);

    m_context_menu.append(
        m_settings_item);
    m_context_menu.append(
        m_session_separator);
    m_context_menu.append(
        m_session_item);
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
                schedule_open_settings();
            });

    m_session_item
        .signal_activate()
        .connect(
            sigc::mem_fun(
                *this,
                &DockHomeItem::open_session));

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

    m_context_menu_map =
        m_context_menu.signal_map().connect(
            [this]()
            {
                if (m_context_menu_mapped)
                    return;

                m_context_menu_mapped = true;
                m_dock.inhibit_autohide();
            });

    m_context_menu_unmap =
        m_context_menu.signal_unmap().connect(
            [this]()
            {
                if (!m_context_menu_mapped)
                    return;

                m_context_menu_mapped = false;
                m_dock.uninhibit_autohide();
            });
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

    // GtkMenu takes a pointer grab, so the dock may not receive a leave event
    // while an action runs. Clear the frame before presenting the popup.
    m_dock.reset_magnified_hover();
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

void DockHomeItem::schedule_open_settings()
{
    // Remote actions can arrive while the modal dialog runs its nested loop.
    if (m_settings_open)
        return;

    // A modal dialog takes the pointer grab away from the dock, so no leave
    // event is guaranteed while it opens. Clear the current frame before the
    // grab instead of leaving a magnified Home icon behind the dialog.
    m_dock.reset_magnified_hover();
    m_dock.hide_tooltip_immediately();
    m_settings_idle.disconnect();

    m_settings_idle =
        Glib::signal_idle().connect(
            [this]()
            {
                m_settings_open = true;
                open_settings();
                m_settings_open = false;
                return false;
            });
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

void DockHomeItem::open_session()
{
    if (m_session_open)
        return;
    m_session_open = true;

    m_dock.inhibit_autohide();
    DockSessionDialog::show(
        m_dock,
        m_source_icon,
        m_window_registry,
        m_dock.launcher_manager(),
        [this](const SessionRecord &)
        {
            m_dock.synchronize_session_items();
        });
    m_dock.uninhibit_autohide();
    m_session_open = false;
}

void DockHomeItem::open_settings()
{
    m_dock.inhibit_autohide();
    DockSettingsDialog::show(
        m_dock,
        m_source_icon,
        m_dock.effective_autohide_effect(),
        m_dock.configurable_autohide_effects());
    // Settings can close without applying a configuration, so enforce the
    // normal frame again after the modal grab is released as well.
    m_dock.reset_magnified_hover();
    m_dock.uninhibit_autohide();
}

void DockHomeItem::show_about()
{
    if (m_about_open)
        return;
    m_about_open = true;

    m_dock.inhibit_autohide();
    DockAboutDialog::show(
        m_dock,
        m_source_icon,
        m_runtime_info);
    m_dock.uninhibit_autohide();
    m_about_open = false;
}

void DockHomeItem::exit_docklight()
{
    // Gio quit alone does not unwind the nested loops of modal GTK dialogs.
    m_settings_idle.disconnect();
    for (auto *window : Gtk::Window::list_toplevels())
    {
        if (auto *dialog = dynamic_cast<Gtk::Dialog *>(window))
            dialog->response(Gtk::RESPONSE_CANCEL);
    }

    auto application =
        Gio::Application::get_default();

    if (application)
        application->quit();
    else
        m_dock.hide();
}
