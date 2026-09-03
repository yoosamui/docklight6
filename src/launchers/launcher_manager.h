// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// launcher_manager.h
//
// Purpose:
// Declares discovery and persistence of launchers attached to the dock.
//
// Responsibilities:
// - Read and preserve the user's launcher order.
// - Resolve desktop identifiers to installed Gio applications.
// - Attach, detach, and reorder launchers atomically.
// - Invalidate the application cache when desktop entries change.
//
// Dependencies and ownership:
// The manager owns its persisted path, application cache, and GLib
// monitor subscription. Gio application objects use shared references.
//
// Design notes:
// Identifier normalization is centralized so configuration, running
// windows, drag payloads, and installed desktop files compare reliably.
//
// ------------------------------------------------------------

#ifndef LAUNCHER_MANAGER_H
#define LAUNCHER_MANAGER_H

#include "session_record.h"

#include <giomm.h>

#include <string>
#include <vector>

class LauncherManager
{
public:
    explicit LauncherManager(
        std::string data_path = {});
    ~LauncherManager();

    std::vector<std::string>
    attached_ids() const;

    // Sessions share docklight.data with the attached launcher list. Both
    // sides are read and rewritten together so a launcher reorder cannot drop
    // the Session block and a Session save cannot drop the launcher order.
    std::vector<SessionRecord> sessions() const;
    std::vector<std::string> session_names() const;
    bool save_session(
        const SessionRecord &session);
    bool rename_session(
        const std::string &old_name,
        const SessionRecord &session);
    bool remove_session(
        const std::string &name);
    // Reorders the stored Sessions to match the given names. Names that are
    // not stored are ignored and stored Sessions that are missing from the
    // list keep their relative order at the end.
    bool reorder_sessions(
        const std::vector<std::string> &names);
    std::vector<std::string> dock_order() const;
    bool reorder_dock_items(
        const std::vector<std::string>
            &desktop_ids);

    Glib::RefPtr<Gio::AppInfo>
    find_application(
        const std::string &desktop_id) const;

    bool set_attached(
        const std::string &desktop_id,
        bool attached);
    bool reorder_attached(
        const std::vector<std::string>
            &desktop_ids);
    bool is_attached(
        const std::string &desktop_id) const;

    static std::string
    normalize_desktop_id(
        const std::string &desktop_id);
    static bool is_transient_window_id(
        const std::string &desktop_id);
    std::string normalize_resolved_id(
        const std::string &desktop_id) const;

private:
    static void on_applications_changed(
        GAppInfoMonitor *monitor,
        gpointer user_data);

    const std::vector<
        Glib::RefPtr<Gio::AppInfo>> &
    applications() const;

    // The complete parsed file. Every read goes through this so no persisted
    // part can be written back without the others.
    struct StoredData
    {
        std::vector<std::string> desktop_ids;
        std::vector<SessionRecord> sessions;
        std::vector<std::string> dock_order;
    };

    StoredData read_data() const;
    bool write_data(
        const StoredData &data) const;

    std::vector<std::string>
    read_config() const;
    bool write_config(
        const std::vector<std::string>
            &desktop_ids) const;

private:
    std::string m_data_path;

    mutable std::vector<
        Glib::RefPtr<Gio::AppInfo>>
        m_applications;
    mutable bool m_applications_loaded =
        false;

    GAppInfoMonitor *m_app_info_monitor =
        nullptr;
    gulong m_app_info_changed_handler = 0;
};

#endif
