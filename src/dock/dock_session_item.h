// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_session_item.h
//
// Purpose:
// Declares the dock item that represents one saved Session.
//
// Responsibilities:
// - Show the Session's own image instead of an application icon.
// - List the Session's persisted items as the dynamic context-menu rows.
// - Own the windows the Session itself launched, and nothing else.
// - Start every persisted item when the Session is activated.
//
// Dependencies and ownership:
// Borrows DockWindow and WindowRegistry through DockItem. Owns its Session
// record, its SessionLauncher, and the bindings from stored items to the
// windows that launching them produced.
//
// Design notes:
// A stored item read from docklight.data corresponds to no live window. There
// is nothing in the file that can identify one: the saved title is the caption
// captured at save time and stops matching the moment the window retitles
// itself, and the application alone would claim windows the user opened. The
// only moment a Session can learn that a window is its own is when its own
// launcher starts it, so SessionLauncher reports each window it identifies and
// the binding is kept here for the lifetime of that window.
//
// The dynamic context-menu rows therefore always come from the persisted
// items, one row per item, so an item that has not been launched is still
// listed. A row that has acquired a window shows that window's live state and
// activating it shows or minimizes the window; a row without one launches its
// item.
//
// ------------------------------------------------------------

#pragma once

#include "dock_item.h"
#include "launchers/session_launcher.h"
#include "launchers/session_record.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

class DockWindow;
class WindowRegistry;

class DockSessionItem : public DockItem
{
public:
    DockSessionItem(
        DockWindow &dock,
        WindowRegistry *window_registry,
        SessionRecord session,
        int icon_size,
        DockHoverEffect hover_effect,
        DockIndicator indicator,
        const std::string &indicator_color);

    void set_session(SessionRecord session);
    const std::string &session_name() const;

    // The identifier a Session item carries in the dock. It is prefixed so it
    // can never collide with, or be persisted as, an attached desktop ID.
    static std::string session_desktop_id(
        const std::string &name);
    static bool is_session_desktop_id(
        const std::string &desktop_id);

    void reload_icon() override;
    Glib::ustring app_name() const override;

protected:
    bool has_edit_action() const override;
    void edit_item() override;
    Glib::RefPtr<Gdk::Pixbuf>
    context_menu_entry_icon(
        const ApplicationWindowEntry &entry)
        const override;
    std::vector<ApplicationWindowEntry>
    context_menu_entries() const override;
    void activate_context_menu_entry(
        const ApplicationWindowEntry &entry)
        override;

private:
    void launch_application() override;
    void launch_stored_item(
        std::size_t index);
    void launch_next_stored_item();
    void on_launch_finished(
        std::string tag);

    // A row stands for a stored item, not for a live window, so it needs an
    // identity of its own. The position in the persisted list is that
    // identity: it survives for as long as the record does and it keeps two
    // identical records distinguishable.
    static std::string stored_item_id(
        std::size_t index);
    const SessionItemRecord *stored_item(
        const std::string &entry_id) const;

    void on_window_identified(
        std::string tag,
        WindowId window_id);
    // The window a stored item launched, or nullptr once that window is gone.
    const ManagedWindow *launched_window(
        const std::string &entry_id) const;
    bool owns_window(
        const WindowId &window_id) const;

    static std::vector<std::string>
    session_identifiers(
        const SessionRecord &session);
    static std::vector<std::string>
    item_identifiers(
        const SessionItemRecord &item);

    SessionRecord m_session;
    SessionLauncher m_launcher;
    WindowRegistry *m_session_window_registry =
        nullptr;
    // Stored-item row identity to the window launching it produced.
    std::map<std::string, WindowId>
        m_launched_windows;
    std::size_t m_next_launch_index = 0;
    bool m_launching_session = false;
};
