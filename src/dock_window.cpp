#include "dock_window.h"

#include "dock_constants.h"
#include "dock_layout_metrics.h"
#include "dock_window_controller.h"
#include "launcher_manager.h"
#include "running_application.h"
#include "window_registry.h"

#include <gtk-layer-shell.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

    gtk_layer_init_for_window(gtk_win);
    gtk_layer_set_keyboard_mode(
        gtk_win,
        GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);

    gtk_layer_set_monitor(
        gtk_win,
        monitor
            ? monitor->gobj()
            : nullptr);

    m_overlay_window.set_monitor(
        monitor);

    gtk_layer_set_namespace(
        gtk_win,
        "docklight6");

    gtk_layer_set_layer(
        gtk_win,
        GTK_LAYER_SHELL_LAYER_TOP);

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

void DockWindow::schedule_show_tooltip(
    DockItem &item)
{
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

void DockWindow::schedule_hide_tooltip()
{
    m_controller->schedule_hide_tooltip();
}

void DockWindow::hide_tooltip_immediately()
{
    m_controller->hide_tooltip_immediately();
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
    m_dragged_item = &item;
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
        return apply_dragged_item_order(
            items);

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

    return apply_dragged_item_order(
        items);
}

void DockWindow::end_item_drag(
    DockItem &item)
{
    if (m_dragged_item == &item)
        m_dragged_item = nullptr;
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

    return apply_dragged_item_order(
        items);
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

void DockWindow::apply_dock_layout(
    const DockPlacement &placement)
{
    apply_visual_style();
    apply_dock_orientation(
        placement.orientation);

    GtkWindow *gtk_win =
        GTK_WINDOW(gobj());

    gtk_layer_set_anchor(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_LEFT,
        placement.anchor_left);

    gtk_layer_set_anchor(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        placement.anchor_right);

    gtk_layer_set_anchor(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_TOP,
        placement.anchor_top);

    gtk_layer_set_anchor(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        placement.anchor_bottom);

    gtk_layer_set_margin(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_LEFT,
        placement.margin_left);

    gtk_layer_set_margin(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_RIGHT,
        placement.margin_right);

    gtk_layer_set_margin(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_TOP,
        placement.margin_top);

    gtk_layer_set_margin(
        gtk_win,
        GTK_LAYER_SHELL_EDGE_BOTTOM,
        placement.margin_bottom);

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

    m_visual_css->load_from_data(
        "window.dock-window {"
        " background-color: transparent;"
        " border-radius: " +
        std::to_string(effective_radius) +
        "px;"
        "}"
        ".dock-surface {"
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
                LauncherManager::
                    normalize_desktop_id(
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

        const auto normalized_id =
            LauncherManager::
                normalize_desktop_id(
                    desktop_id);

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

        auto app =
            m_launcher_manager
                .find_application(
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
