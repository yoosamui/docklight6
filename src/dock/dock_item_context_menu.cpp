// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_item_context_menu.cpp
//
// Implementation overview:
// Implements DockItem context-menu construction, dynamic window entries,
// deferred menu actions, and application launching.
//
// ------------------------------------------------------------

#include "dock_item.h"
#include "dock_window.h"
#include "presentation/presentation_selector.h"

#include <gio/gdesktopappinfo.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{

    constexpr int CONTEXT_MENU_ICON_SIZE = 20; // Window icon size in menu rows
    constexpr int CONTEXT_MENU_TITLE_WIDTH = 48; // Maximum menu title width in characters
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

        prepare_application_launch_context(
            G_APP_LAUNCH_CONTEXT(
                context->gobj()));

        return context;
    }


} // namespace

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

                // Dynamic window entries run after GtkMenu releases its
                // popup grab. Preserve the menu's autohide inhibition until
                // that deferred window action has completed.
                if (m_context_menu_window_action_pending)
                    return;

                if (reopen_preview)
                    m_dock.schedule_show_tooltip(
                        *this);

                // GtkMenu may unmap before it emits the selected dynamic
                // item's activate signal. Defer the release until all menu
                // signals from this event have run, when the pending window
                // action flag is authoritative.
                m_context_menu_uninhibit_idle.disconnect();
                m_context_menu_uninhibit_idle =
                    Glib::signal_idle().connect(
                        [this,
                         reopen_preview]()
                        {
                            if (!m_context_menu_window_action_pending)
                            {
                                if (reopen_preview)
                                {
                                    m_dock.uninhibit_autohide(true);
                                }
                                else
                                {
                                    m_dock.uninhibit_autohide();
                                }
                            }

                            return false;
                        });
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
    m_context_menu_window_action_pending = true;
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

                m_context_menu_window_action_pending = false;
                m_context_menu_uninhibit_idle.disconnect();
                m_dock.uninhibit_autohide();

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
