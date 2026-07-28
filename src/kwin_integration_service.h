#pragma once

#include "kwin_window_command.h"
#include "window_icon_geometry.h"

#include <gio/gio.h>
#include <sigc++/connection.h>

#include <deque>
#include <map>
#include <string>

class KWinWindowBackend;

class KWinIntegrationService
{
public:
    explicit KWinIntegrationService(
        KWinWindowBackend &backend);
    ~KWinIntegrationService();

    bool start();
    void stop();

    bool available() const;

private:
    bool request_name();
    void release_name();
    bool register_object();
    void unregister_object();

    bool register_sender(
        const char *sender,
        unsigned int protocol_version);
    bool accepts_sender(
        const char *sender) const;
    void clear_sender(
        bool unregister_integration);

    bool enqueue_command(
        const KWinWindowCommand &command);
    bool publish_icon_geometry(
        const WindowId &window_id,
        const WindowIconGeometry &geometry);
    void remove_icon_geometry(
        const WindowId &window_id);
    void handle_backend_connection(
        bool connected);
    void prune_icon_geometries();
    void schedule_effect_geometry_update();
    void publish_effect_geometries();
    bool write_effect_geometries(
        const std::string &geometries);
    void cancel_effect_geometry_update();
    void emit_icon_geometry_removed(
        const WindowId &window_id);
    void return_icon_geometries(
        GDBusMethodInvocation *invocation) const;
    void return_dock_surface_geometry(
        GDBusMethodInvocation *invocation) const;
    void emit_dock_surface_geometry();
    void deliver_next_command();
    void cancel_command_keepalive();
    void clear_commands();

    void handle_method_call(
        const char *sender,
        const char *method_name,
        GVariant *parameters,
        GDBusMethodInvocation *invocation);

    static void on_sender_vanished(
        GDBusConnection *connection,
        const gchar *name,
        gpointer user_data);
    static gboolean on_command_keepalive(
        gpointer user_data);
    static gboolean on_effect_geometry_update(
        gpointer user_data);
    static void on_method_call(
        GDBusConnection *connection,
        const gchar *sender,
        const gchar *object_path,
        const gchar *interface_name,
        const gchar *method_name,
        GVariant *parameters,
        GDBusMethodInvocation *invocation,
        gpointer user_data);

private:
    KWinWindowBackend &m_backend;

    GDBusNodeInfo *m_introspection = nullptr;
    GDBusConnection *m_connection = nullptr;

    std::string m_sender;
    std::string
        m_published_effect_geometries;

    std::deque<KWinWindowCommand>
        m_commands;

    std::map<WindowId, WindowIconGeometry>
        m_icon_geometries;

    sigc::connection
        m_window_removed;
    sigc::connection
        m_snapshot_changed;
    sigc::connection
        m_connection_changed;
    sigc::connection
        m_dock_surface_geometry_changed;

    GDBusMethodInvocation *
        m_command_invocation = nullptr;

    guint m_object_registration_id = 0;
    guint m_sender_watch_id = 0;
    guint m_command_keepalive_id = 0;
    guint m_effect_geometry_update_id = 0;

    bool m_name_owned = false;
    bool m_available = false;
    bool m_effect_geometries_initialized =
        false;
};
