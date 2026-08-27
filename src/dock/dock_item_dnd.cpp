// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_item_dnd.cpp
//
// Implementation overview:
// Implements DockItem drag-source and drag-destination behavior.
//
// ------------------------------------------------------------

#include "dock_item.h"
#include "dock_constants.h"
#include "dock_window.h"

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
