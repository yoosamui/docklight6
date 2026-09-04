// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_session_item.cpp
//
// Implementation overview:
// Implements the dock item for one saved Session: its image, its display
// name, the rows its context menu lists, which live windows it owns, and what
// activating it starts.
//
// Important implementation decisions:
// - A window becomes the Session's only when the Session's own launcher starts
//   it. SessionLauncher reports each window it identifies together with the
//   tag the launch carried, and that tag is the stored item's row identity.
//   The grouped identifiers still name the Session's applications so the
//   controller can find candidate windows, but a window filter narrows the
//   group to the launched set, so a window the user opened is never claimed.
// - Nothing is attributed after a Docklight restart until the Session is
//   launched again. That is deliberate. Two alternatives were tried and both
//   hid stored items: matching a live window by application alone claimed
//   windows the Session never started, and matching by the saved title stopped
//   working the moment a window retitled itself, which a terminal, a browser,
//   or an editor does constantly.
// - The dynamic context-menu rows always come from the persisted items, so an
//   item that has not been launched is still listed. A row holding a live
//   window shows that window's caption and state and activating it shows or
//   minimizes the window; a row without one launches its item.
// - The image is loaded from the Session's stored path and scaled through
//   DockItem::apply_icon_pixbuf(), which rebuilds the hover and animation
//   frames exactly as an application icon would.
// - A missing or unreadable image falls back to a themed icon rather than
//   leaving an unclickable gap.
//
// ------------------------------------------------------------

#include "dock_session_item.h"
#include "dock_constants.h"
#include "dock_window.h"
#include "launchers/launcher_manager.h"
#include "windowing/window_registry.h"

#include <glibmm/miscutils.h>

#include <algorithm>
#include <utility>

namespace
{
constexpr const char *SESSION_ID_PREFIX = "session:";
constexpr const char *SESSION_ITEM_ID_PREFIX =
    "session-item:";
constexpr const char *SESSION_FALLBACK_ICON =
    "application-x-executable";

bool same_items(
    const std::vector<SessionItemRecord> &left,
    const std::vector<SessionItemRecord> &right)
{
    if (left.size() != right.size())
        return false;

    for (std::size_t index = 0;
         index < left.size();
         ++index)
    {
        const auto &a = left[index];
        const auto &b = right[index];

        if (a.desktop_file != b.desktop_file ||
            a.title != b.title ||
            a.parameters != b.parameters ||
            a.workspace != b.workspace ||
            a.dimensions != b.dimensions ||
            a.position != b.position)
        {
            return false;
        }
    }

    return true;
}
}

std::string DockSessionItem::session_desktop_id(
    const std::string &name)
{
    return std::string(SESSION_ID_PREFIX) + name;
}

bool DockSessionItem::is_session_desktop_id(
    const std::string &desktop_id)
{
    const std::string prefix =
        SESSION_ID_PREFIX;

    return desktop_id.size() > prefix.size() &&
           desktop_id.compare(
               0,
               prefix.size(),
               prefix) == 0;
}

std::string DockSessionItem::stored_item_id(
    std::size_t index)
{
    return std::string(
               SESSION_ITEM_ID_PREFIX) +
           std::to_string(index);
}

const SessionItemRecord *
DockSessionItem::stored_item(
    const std::string &entry_id) const
{
    for (std::size_t index = 0;
         index < m_session.items.size();
         ++index)
    {
        if (stored_item_id(index) == entry_id)
            return &m_session.items[index];
    }

    return nullptr;
}

// The identities one stored item's window may report. The editor stores an
// absolute .desktop path while a window reports a desktop ID, an executable,
// or a WM_CLASS, so the stored value alone matches nothing. Themed icon names
// are excluded: generic names such as "utilities-terminal" are shared by
// unrelated programs and a Session spans several applications.
std::vector<std::string>
DockSessionItem::item_identifiers(
    const SessionItemRecord &item)
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

    if (item.desktop_file.empty())
        return identifiers;

    const auto application =
        find_session_application(
            item.desktop_file);

    for (const auto &identifier :
         application_identifiers(
             application,
             item.desktop_file,
             false))
    {
        add_identifier(identifier);
    }

    if (Glib::path_is_absolute(
            item.desktop_file))
    {
        add_identifier(
            Glib::path_get_basename(
                item.desktop_file));
    }

    return identifiers;
}

std::vector<std::string>
DockSessionItem::session_identifiers(
    const SessionRecord &session)
{
    std::vector<std::string> identifiers;

    for (const auto &item : session.items)
    {
        for (const auto &identifier :
             item_identifiers(item))
        {
            if (std::find(
                    identifiers.begin(),
                    identifiers.end(),
                    identifier) == identifiers.end())
            {
                identifiers.push_back(identifier);
            }
        }
    }

    return identifiers;
}

DockSessionItem::DockSessionItem(
    DockWindow &dock,
    WindowRegistry *window_registry,
    SessionRecord session,
    int icon_size,
    DockHoverEffect hover_effect,
    DockIndicator indicator,
    const std::string &indicator_color)
    : DockItem(
          dock,
          session_desktop_id(session.name),
          session_identifiers(session),
          window_registry,
          icon_size,
          hover_effect,
          indicator,
          indicator_color),
      m_session(std::move(session)),
      m_launcher(window_registry),
      m_session_window_registry(window_registry)
{
    m_launcher.signal_window_identified()
        .connect(
            sigc::mem_fun(
                *this,
                &DockSessionItem::
                    on_window_identified));
    m_launcher.signal_launch_finished()
        .connect(
            sigc::mem_fun(
                *this,
                &DockSessionItem::
                    on_launch_finished));

    // The identifiers above only make the Session's applications findable.
    // Ownership is the launched set, so a window the user opened is excluded
    // from the indicator, the previews, and the group window actions.
    set_window_filter(
        [this](const ManagedWindow &window)
        {
            return owns_window(window.id);
        });

    reload_icon();
}

const std::string &
DockSessionItem::session_name() const
{
    return m_session.name;
}

void DockSessionItem::set_session(
    SessionRecord session)
{
    const bool icon_changed =
        session.icon != m_session.icon;
    auto identifiers =
        session_identifiers(session);

    // Row identity is a position in the stored list, so an edit that adds,
    // removes, or reorders items would silently rebind a window to the wrong
    // record. Drop the bindings in that case only. The dock re-applies the
    // stored record on every synchronization pass, and launching a Session is
    // itself what changes the running set that triggers one, so clearing
    // unconditionally would erase each binding moments after it was made.
    if (!same_items(
            session.items,
            m_session.items))
    {
        m_launched_windows.clear();
    }

    m_session = std::move(session);
    set_application_identifiers(
        std::move(identifiers));

    if (icon_changed)
        reload_icon();
}

void DockSessionItem::on_window_identified(
    std::string tag,
    WindowId window_id)
{
    if (tag.empty())
        return;

    m_launched_windows[tag] =
        std::move(window_id);

    // The group just gained a window: refresh the indicator that the empty
    // launched set had kept dark.
    refresh_indicator();
}

const ManagedWindow *
DockSessionItem::launched_window(
    const std::string &entry_id) const
{
    if (!m_session_window_registry)
        return nullptr;

    const auto launched =
        m_launched_windows.find(entry_id);

    if (launched == m_launched_windows.end())
        return nullptr;

    for (const auto &window :
         m_session_window_registry->windows())
    {
        if (window.id == launched->second)
            return &window;
    }

    // The window is closed. The binding is left in place rather than pruned
    // here, because this runs from const paths; it is overwritten by the next
    // launch of the same item and ignored until then.
    return nullptr;
}

bool DockSessionItem::owns_window(
    const WindowId &window_id) const
{
    for (const auto &launched :
         m_launched_windows)
    {
        if (launched.second == window_id)
            return true;
    }

    return false;
}

// One row per persisted item, in stored order. A row that has acquired a
// window reports that window's live state; a row without one falls back to the
// saved title, or to the application's display name when nothing was captured,
// so a row is never blank.
std::vector<ApplicationWindowEntry>
DockSessionItem::context_menu_entries() const
{
    std::vector<ApplicationWindowEntry> entries;
    entries.reserve(m_session.items.size());

    for (std::size_t index = 0;
         index < m_session.items.size();
         ++index)
    {
        const auto &item =
            m_session.items[index];

        ApplicationWindowEntry entry;
        entry.id = stored_item_id(index);
        entry.desktop_file_name =
            item.desktop_file;

        const auto *window =
            launched_window(entry.id);

        if (window)
        {
            entry.caption = window->caption;
            entry.icon_name = window->icon_name;
            entry.desktop_numbers =
                window->desktop_numbers;
            entry.active = window->active;
            entry.minimized = window->minimized;
            entry.on_current_desktop =
                window->on_current_desktop;
        }
        else
        {
            entry.caption = item.title;
        }

        if (entry.caption.empty())
        {
            const auto application =
                find_session_application(
                    item.desktop_file);

            entry.caption =
                application
                    ? application
                          ->get_display_name()
                    : item.desktop_file;
        }

        entries.push_back(std::move(entry));
    }

    return entries;
}

void DockSessionItem::activate_context_menu_entry(
    const ApplicationWindowEntry &entry)
{
    const auto *window =
        launched_window(entry.id);

    if (window)
    {
        schedule_window_action(
            window->id,
            window->active &&
                !window->minimized);
        return;
    }

    for (std::size_t index = 0;
         index < m_session.items.size();
         ++index)
    {
        if (stored_item_id(index) == entry.id)
        {
            launch_stored_item(index);
            return;
        }
    }
}

Glib::RefPtr<Gdk::Pixbuf>
DockSessionItem::context_menu_entry_icon(
    const ApplicationWindowEntry &entry) const
{
    const auto *item = stored_item(entry.id);

    if (item && !entry.minimized)
    {
        const auto application =
            find_session_application(
                item->desktop_file);
        const auto theme =
            Gtk::IconTheme::get_default();

        if (application && theme &&
            application->get_icon())
        {
            try
            {
                const auto info = theme->lookup_icon(
                    application->get_icon(),
                    DockConstants::CONTEXT_MENU_ICON_SIZE,
                    Gtk::ICON_LOOKUP_USE_BUILTIN);
                if (info)
                {
                    const auto icon = info.load_icon();
                    if (icon)
                        return icon;
                }
            }
            catch (const Glib::Error &)
            {
            }
        }
    }

    return DockItem::context_menu_entry_icon(
        entry);
}

Glib::ustring DockSessionItem::app_name() const
{
    return m_session.name;
}

bool DockSessionItem::has_edit_action() const
{
    return true;
}

void DockSessionItem::edit_item()
{
    dock().edit_session(m_session.name);
}

void DockSessionItem::reload_icon()
{
    const int size = icon_size();

    if (size <= 0)
        return;

    if (!m_session.icon.empty())
    {
        try
        {
            apply_icon_pixbuf(
                Gdk::Pixbuf::create_from_file(
                    m_session.icon,
                    size,
                    size,
                    true));
            return;
        }
        catch (const Glib::Error &)
        {
            g_warning(
                "Cannot load Session icon '%s'",
                m_session.icon.c_str());
        }
    }

    try
    {
        const auto theme =
            Gtk::IconTheme::get_default();

        if (theme)
        {
            apply_icon_pixbuf(
                theme->load_icon(
                    SESSION_FALLBACK_ICON,
                    size,
                    Gtk::ICON_LOOKUP_FORCE_SIZE));
        }
    }
    catch (const Glib::Error &)
    {
        // Leave the previous image rather than blanking the item.
    }
}

// Each item is launched under its own row identity, which is what lets the
// launcher report back which window that item produced.
void DockSessionItem::launch_stored_item(
    std::size_t index)
{
    if (index >= m_session.items.size())
        return;

    const auto &item =
        m_session.items[index];
    const auto error =
        m_launcher.launch(
            item,
            stored_item_id(index));

    if (!error.empty())
    {
        g_warning(
            "Cannot start Session item '%s': %s",
            item.desktop_file.c_str(),
            error.c_str());
    }
}

void DockSessionItem::launch_next_stored_item()
{
    if (!m_launching_session ||
        m_next_launch_index >= m_session.items.size())
    {
        m_launching_session = false;
        return;
    }

    const auto index = m_next_launch_index++;
    const auto &item = m_session.items[index];
    const auto error =
        m_launcher.launch(
            item,
            stored_item_id(index));

    if (!error.empty())
    {
        g_warning(
            "Cannot start Session item '%s': %s",
            item.desktop_file.c_str(),
            error.c_str());
    }

    if (!error.empty() ||
        !m_launcher.tracks_windows())
    {
        Glib::signal_idle().connect_once(
            [this]()
            {
                launch_next_stored_item();
            });
    }
}

void DockSessionItem::on_launch_finished(
    std::string)
{
    // The signal is emitted while SessionLauncher is draining its pending
    // vector. Defer the next launch so it cannot invalidate that iteration.
    Glib::signal_idle().connect_once(
        [this]()
        {
            launch_next_stored_item();
        });
}

void DockSessionItem::launch_application()
{
    if (m_launching_session)
        return;

    m_next_launch_index = 0;
    m_launching_session = true;
    launch_next_stored_item();
}
