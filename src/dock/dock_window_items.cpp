// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window_items.cpp
//
// Implementation overview:
// Implements DockWindow item registration, application resolution,
// launcher synchronization, and persistent dock-container construction.
//
// - Dock item identity is based on normalized desktop identifiers.
// - Item synchronization preserves configured order while merging apps.
//
// ------------------------------------------------------------

#include "dock_window.h"
#include "dock_home_item.h"
#include "dock_session_item.h"

#include "application/dock_runtime_info.h"
#include "dock_constants.h"
#include "dock_window_controller.h"
#include "windowing/running_application.h"
#include "windowing/window_registry.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

const std::vector<DockItem *> &
DockWindow::dock_items() const
{
    return m_dock_items_cache;
}

void DockWindow::register_dock_item(
    DockItem *item)
{
    if (!item ||
        std::find(
            m_dock_items_cache.begin(),
            m_dock_items_cache.end(),
            item) != m_dock_items_cache.end())
    {
        return;
    }

    // Update the typed view first: signal_add() observers can query the dock
    // synchronously while Gtk::Box::pack_start() is still on the stack.
    m_dock_items_cache.push_back(item);
    m_dock_box.pack_start(
        *item,
        Gtk::PACK_SHRINK);
    item->show();
}

void DockWindow::unregister_dock_item(
    DockItem *item)
{
    if (!item)
        return;

    const auto item_position =
        std::find(
            m_dock_items_cache.begin(),
            m_dock_items_cache.end(),
            item);
    if (item_position == m_dock_items_cache.end())
        return;

    // Gtk::manage() allows removal to destroy the child. Erase the borrowed
    // pointer before remove() and before signal_remove() observers run.
    m_dock_items_cache.erase(item_position);
    m_dock_box.remove(*item);
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

// The Session editor calls this after writing a Session. Sessions take part in
// the ordinary item synchronization, so this only has to force a pass that the
// unchanged attached/running snapshots would otherwise skip.
void DockWindow::synchronize_session_items()
{
    m_has_synchronized_items = false;
    synchronize_dock_items();
}

void DockWindow::synchronize_dock_items()
{
    struct DesiredItem
    {
        std::string desktop_id;
        Glib::RefPtr<Gio::AppInfo> app;
        bool attached = false;
        bool is_session = false;
        SessionRecord session;
    };

    std::vector<DesiredItem> desired_items;

    // Saved Sessions are stored after the launcher lines in docklight.data.
    // Collect their identities here for change detection, then append the
    // actual desired items after ordinary dock items below.
    const auto sessions =
        m_launcher_manager.sessions();

    std::vector<std::string> session_ids;
    session_ids.reserve(sessions.size());

    for (const auto &session : sessions)
    {
        const auto desktop_id =
            DockSessionItem::session_desktop_id(
                session.name);

        session_ids.push_back(desktop_id);
    }

    const auto attached_ids =
        m_launcher_manager
            .attached_ids();
    const auto stored_dock_order =
        m_launcher_manager.dock_order();

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
            m_synchronized_running_ids &&
        session_ids == m_synchronized_session_ids &&
        stored_dock_order ==
            m_synchronized_dock_order)
    {
        return;
    }

    m_synchronized_attached_ids =
        normalized_attached_ids;
    m_synchronized_running_ids =
        normalized_running_ids;
    m_synchronized_session_ids = session_ids;
    m_synchronized_dock_order =
        stored_dock_order;
    m_has_synchronized_items = true;

    const int maximum_items =
        std::max(
            0,
            DockConstants::MAX_DOCK_ITEMS -
                1);

    const auto &current_items =
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

        // Sessions are appended after every ordinary item below, matching
        // their position after the launcher list in docklight.data.
        if (DockSessionItem::is_session_desktop_id(
                item->desktop_id()))
        {
            continue;
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
             attached,
             false,
             {}});
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
             true,
             false,
             {}});
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
                 false,
                 false,
                 {}});
        }
    }

    for (const auto &session : sessions)
    {
        if (static_cast<int>(
                desired_items.size()) >=
            maximum_items)
        {
            break;
        }

        desired_items.push_back(
            {DockSessionItem::session_desktop_id(
                 session.name),
             {},
             true,
             true,
             session});
    }

    // Apply the explicitly persisted mixed order. Old files synthesize the
    // compatibility order (launchers, then Sessions) in dock_order(). Any
    // live, unattached applications are not persistent and remain afterward.
    std::vector<DesiredItem> ordered_desired_items;
    ordered_desired_items.reserve(desired_items.size());

    for (const auto &stored_id : stored_dock_order)
    {
        const bool stored_is_session =
            DockSessionItem::is_session_desktop_id(
                stored_id);
        const auto normalized_id = stored_is_session
            ? stored_id
            : m_launcher_manager.normalize_resolved_id(
                  stored_id);

        const auto desired = std::find_if(
            desired_items.begin(),
            desired_items.end(),
            [this, stored_is_session, &normalized_id](
                const DesiredItem &candidate)
            {
                if (candidate.is_session !=
                    stored_is_session)
                {
                    return false;
                }

                return stored_is_session
                    ? candidate.desktop_id == normalized_id
                    : m_launcher_manager.normalize_resolved_id(
                          candidate.desktop_id) ==
                          normalized_id;
            });

        if (desired == desired_items.end())
            continue;

        ordered_desired_items.push_back(
            std::move(*desired));
        desired_items.erase(desired);
    }

    ordered_desired_items.insert(
        ordered_desired_items.end(),
        std::make_move_iterator(desired_items.begin()),
        std::make_move_iterator(desired_items.end()));
    desired_items =
        std::move(ordered_desired_items);

    // The coarse attached/running snapshots above avoid building this list
    // for title-only registry updates. This structural diff handles the
    // remaining case where a snapshot changed but resolved to the same live
    // widget sequence and attachment state.
    bool dock_structure_changed =
        current_items.size() != desired_items.size();

    if (!dock_structure_changed)
    {
        for (std::size_t index = 0;
             index < current_items.size();
             ++index)
        {
            const auto current_id =
                LauncherManager::normalize_desktop_id(
                    current_items[index]->desktop_id());
            const auto desired_id =
                LauncherManager::normalize_desktop_id(
                    desired_items[index].desktop_id);

            if (current_id != desired_id ||
                current_items[index]->attached() !=
                    desired_items[index].attached)
            {
                dock_structure_changed = true;
                break;
            }
        }
    }

    if (!dock_structure_changed)
    {
        for (std::size_t index = 0;
             index < current_items.size();
             ++index)
        {
            if (desired_items[index].is_session)
            {
                static_cast<DockSessionItem *>(
                    current_items[index])
                    ->set_session(
                        desired_items[index]
                            .session);
            }
            else
            {
                current_items[index]
                    ->refresh_indicator();
            }
        }
        return;
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

            if (desired.is_session)
            {
                static_cast<DockSessionItem *>(
                    item)
                    ->set_session(
                        desired.session);
            }
        }
        else if (desired.is_session)
        {
            item =
                Gtk::manage(
                    new DockSessionItem(
                        *this,
                        m_window_registry,
                        desired.session,
                        m_effective_icon_size > 0
                            ? m_effective_icon_size
                            : m_controller
                                  ->settings()
                                  .icon_size(),
                        m_controller->settings()
                            .hover_effect(),
                        m_controller->settings()
                            .indicator(),
                        m_controller->settings()
                            .indicator_color()));

            item->set_manage_all_workspaces(
                m_controller->settings()
                    .manage_all_workspaces());

            register_dock_item(item);
            children_changed = true;
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

            register_dock_item(item);
            children_changed = true;
        }

        ordered_items.push_back(item);
    }

    if (!existing_items.empty())
        hide_tooltip_immediately();

    for (auto *item : existing_items)
    {
        unregister_dock_item(item);
        children_changed = true;
    }

    // GTK3 exposes child-notify freezing on each child widget, not on the
    // container. Queue at most one child-property notification per survivor
    // while their positions are updated as one logical transaction.
    for (auto *item : ordered_items)
    {
        gtk_widget_freeze_child_notify(
            GTK_WIDGET(item->gobj()));
    }
    gtk_widget_freeze_child_notify(
        GTK_WIDGET(m_trailing_margin.gobj()));

    int position = 2;

    for (auto *item : ordered_items)
    {
        m_dock_box.reorder_child(
            *item,
            position++);
    }

    m_dock_items_cache = ordered_items;

    m_dock_box.reorder_child(
        m_trailing_margin,
        -1);

    for (auto *item : ordered_items)
    {
        gtk_widget_thaw_child_notify(
            GTK_WIDGET(item->gobj()));
    }
    gtk_widget_thaw_child_notify(
        GTK_WIDGET(m_trailing_margin.gobj()));

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
void DockWindow::create_dock(
    const DockRuntimeInfo &runtime_info)
{
    m_dock_box.pack_start(
        m_leading_margin,
        Gtk::PACK_SHRINK);

    m_home_item =
        Gtk::manage(
            new DockHomeItem(
                *this,
                m_window_registry,
                runtime_info,
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
