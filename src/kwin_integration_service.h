#pragma once

#include <gio/gio.h>

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

    void handle_method_call(
        const char *sender,
        const char *method_name,
        GVariant *parameters,
        GDBusMethodInvocation *invocation);

    static void on_sender_vanished(
        GDBusConnection *connection,
        const gchar *name,
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

    guint m_object_registration_id = 0;
    guint m_sender_watch_id = 0;

    bool m_name_owned = false;
    bool m_available = false;
};
