// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window_dnd.cpp
//
// Implementation overview:
// Implements DockWindow item-reordering drag-and-drop behavior.
//
// Drag reorder writes through LauncherManager before rebuilding widgets.
//
// ------------------------------------------------------------

#include "dock_window.h"
#include "dock_session_item.h"
#include "dock_window_controller.h"

#include <algorithm>
#include <string>
#include <vector>

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

    const auto &items = dock_items();

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

    const auto &items = dock_items();

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

    m_dock_items_cache = items;

    // Persist one combined sequence so a Session may be dropped on either
    // side of an ordinary launcher. Running, unattached applications remain
    // visual-only and are deliberately absent from the saved sequence.
    std::vector<std::string> dock_order;

    for (const auto *item : items)
    {
        if (DockSessionItem::is_session_desktop_id(
                item->desktop_id()) ||
            item->attached())
        {
            dock_order.push_back(
                item->desktop_id());
        }
    }

    if (!m_launcher_manager.reorder_dock_items(
            dock_order))
    {
        g_warning(
            "Cannot persist reordered dock items");
        synchronize_session_items();
        return false;
    }

    // Keep the synchronization snapshot in step with the file written by this
    // drop. Leaving the pre-drag order cached makes the next registry update
    // look like an external configuration change while another drag may
    // already be in progress.
    m_synchronized_dock_order =
        m_launcher_manager.dock_order();

    m_controller->dock_items_reordered();
    return true;
}
