// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// session_launcher.h
//
// Purpose:
// Declares launching of stored Session items with deferred window placement.
//
// Responsibilities:
// - Resolve a stored desktop identity to an installed application.
// - Launch it with the item's parameters appended to the entry's Exec line.
// - Watch the window registry for the resulting window and place it once.
//
// Dependencies and ownership:
// Borrows a WindowRegistry. Owns its pending-placement list and the registry
// subscription that drains it.
//
// Design notes:
// Both the Session editor card and the dock's Session item launch through this
// one implementation, so identity resolution, parameter handling, and
// placement behave identically wherever a Session is started. Several items
// can be in flight at once, which is why placements are tracked as a list
// rather than the single pending launch a one-item editor needed.
//
// ------------------------------------------------------------

#pragma once

#include "session_record.h"
#include "windowing/window_backend.h"

#include <giomm.h>
#include <glibmm/ustring.h>
#include <sigc++/connection.h>
#include <sigc++/signal.h>

#include <set>
#include <string>
#include <vector>

class WindowRegistry;

// Resolves a stored desktop identity to an installed application by path,
// desktop ID, desktop ID plus suffix, and finally a normalized scan over ID,
// name, display name, executable, and StartupWMClass. Exposed so the editor
// shows exactly the application that would be launched.
Glib::RefPtr<Gio::DesktopAppInfo>
find_session_application(
    const std::string &desktop_file_name);

class SessionLauncher
{
public:
    explicit SessionLauncher(
        WindowRegistry *window_registry);
    ~SessionLauncher();

    // Launches one item. Returns an empty string on success, otherwise a
    // translated message describing why nothing was started. Placement is
    // requested later, when the matching window appears.
    //
    // The tag is opaque to the launcher. A caller that needs to know which
    // window its launch produced passes one and reads it back from
    // signal_window_identified(). A caller that only wants the application
    // started leaves it empty.
    Glib::ustring launch(
        const SessionItemRecord &item,
        const std::string &tag = {});

    // Emitted once for each launch whose window the launcher identifies,
    // carrying that launch's tag and the window it produced. This is the only
    // moment a Session can learn that a live window is its own: a window that
    // was already open when the launch started is explicitly excluded, and a
    // stored item read from docklight.data corresponds to no live window at
    // all until it has been launched.
    sigc::signal<
        void,
        std::string,
        WindowId> &
    signal_window_identified();

    // Emitted when a tagged launch either acquired a window or exhausted its
    // tracking deadline. Whole Sessions use this to serialize application
    // startup; launching two instances of the same single-instance
    // application concurrently can otherwise discard one request.
    sigc::signal<void, std::string> &
    signal_launch_finished();

    bool tracks_windows() const;

    // True when the current backend can place windows at all. Callers use this
    // to tell the user that geometry fields were ignored.
    bool can_place() const;

private:
    struct PendingPlacement
    {
        // A backend may report the desktop ID, executable, GTK application
        // ID, or StartupWMClass for the same application.
        std::vector<std::string> application_ids;
        std::string title;
        std::string tag;
        WindowPlacement placement;
        std::set<WindowId> windows_before_launch;
    };

    void begin_tracking(
        PendingPlacement pending);
    void on_window_registry_changed();
    bool on_tracking_timeout();
    void stop_tracking();

    WindowRegistry *m_window_registry = nullptr;
    std::vector<PendingPlacement> m_pending;
    // Reservations survive separate registry callbacks so concurrent Session
    // launches cannot bind multiple rows to one newly-created window.
    std::set<WindowId> m_claimed_window_ids;
    sigc::connection m_window_changed;
    sigc::connection m_timeout;
    sigc::signal<
        void,
        std::string,
        WindowId>
        m_window_identified;
    sigc::signal<void, std::string>
        m_launch_finished;
};
