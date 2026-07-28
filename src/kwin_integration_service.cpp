#include "kwin_integration_service.h"

#include "kwin_integration_protocol.h"
#include "kwin_window_backend.h"

#include <glib.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace
{

constexpr char INTROSPECTION_XML[] =
    "<node>"
    "  <interface name='org.docklight6.WindowIntegration1'>"
    "    <method name='Ping'>"
    "      <arg type='u' direction='out' name='protocol_version'/>"
    "      <arg type='t' direction='out' name='last_revision'/>"
    "    </method>"
    "    <method name='Register'>"
    "      <arg type='u' direction='in' name='protocol_version'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "      <arg type='u' direction='out' name='supported_version'/>"
    "    </method>"
    "    <method name='Unregister'/>"
    "    <method name='BeginSnapshot'>"
    "      <arg type='t' direction='in' name='revision'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "    </method>"
    "    <method name='StageWindow'>"
    "      <arg type='t' direction='in' name='revision'/>"
    "      <arg type='s' direction='in' name='internal_id'/>"
    "      <arg type='s' direction='in' name='desktop_file_name'/>"
    "      <arg type='s' direction='in' name='caption'/>"
    "      <arg type='s' direction='in' name='icon_name'/>"
    "      <arg type='x' direction='in' name='pid'/>"
    "      <arg type='b' direction='in' name='minimized'/>"
    "      <arg type='b' direction='in' name='maximized'/>"
    "      <arg type='b' direction='in' name='skip_taskbar'/>"
    "      <arg type='i' direction='in' name='x'/>"
    "      <arg type='i' direction='in' name='y'/>"
    "      <arg type='i' direction='in' name='width'/>"
    "      <arg type='i' direction='in' name='height'/>"
    "      <arg type='as' direction='in' name='activities'/>"
    "      <arg type='as' direction='in' name='desktops'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "    </method>"
    "    <method name='CommitSnapshot'>"
    "      <arg type='t' direction='in' name='revision'/>"
    "      <arg type='s' direction='in' name='active_window'/>"
    "      <arg type='as' direction='in' name='stacking_order'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "    </method>"
    "    <method name='PublishWindow'>"
    "      <arg type='t' direction='in' name='revision'/>"
    "      <arg type='s' direction='in' name='internal_id'/>"
    "      <arg type='s' direction='in' name='desktop_file_name'/>"
    "      <arg type='s' direction='in' name='caption'/>"
    "      <arg type='s' direction='in' name='icon_name'/>"
    "      <arg type='x' direction='in' name='pid'/>"
    "      <arg type='b' direction='in' name='minimized'/>"
    "      <arg type='b' direction='in' name='maximized'/>"
    "      <arg type='b' direction='in' name='skip_taskbar'/>"
    "      <arg type='i' direction='in' name='x'/>"
    "      <arg type='i' direction='in' name='y'/>"
    "      <arg type='i' direction='in' name='width'/>"
    "      <arg type='i' direction='in' name='height'/>"
    "      <arg type='as' direction='in' name='activities'/>"
    "      <arg type='as' direction='in' name='desktops'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "    </method>"
    "    <method name='PublishWindowRemoved'>"
    "      <arg type='t' direction='in' name='revision'/>"
    "      <arg type='s' direction='in' name='internal_id'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "    </method>"
    "    <method name='PublishActiveWindow'>"
    "      <arg type='t' direction='in' name='revision'/>"
    "      <arg type='s' direction='in' name='internal_id'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "    </method>"
    "    <method name='PublishStackingOrder'>"
    "      <arg type='t' direction='in' name='revision'/>"
    "      <arg type='as' direction='in' name='stacking_order'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "    </method>"
    "  </interface>"
    "</node>";

std::vector<std::string> string_array(
    GVariant *array)
{
    std::vector<std::string> values;
    GVariantIter iterator;
    const char *value = nullptr;

    g_variant_iter_init(
        &iterator,
        array);

    while (g_variant_iter_next(
        &iterator,
        "&s",
        &value))
    {
        values.emplace_back(value);
    }

    return values;
}

std::optional<WindowId> optional_window_id(
    const char *window_id)
{
    if (!window_id ||
        window_id[0] == '\0')
    {
        return std::nullopt;
    }

    return WindowId{window_id};
}

bool parse_window(
    GVariant *parameters,
    std::uint64_t &revision,
    ManagedWindow &window)
{
    const char *internal_id = nullptr;
    const char *desktop_file_name = nullptr;
    const char *caption = nullptr;
    const char *icon_name = nullptr;

    gint64 process_id = 0;

    gboolean minimized = false;
    gboolean maximized = false;
    gboolean skip_taskbar = false;

    gint32 x = 0;
    gint32 y = 0;
    gint32 width = 0;
    gint32 height = 0;

    GVariant *activities = nullptr;
    GVariant *desktops = nullptr;

    guint64 message_revision = 0;

    g_variant_get(
        parameters,
        "(t&s&s&s&sxbbbiiii@as@as)",
        &message_revision,
        &internal_id,
        &desktop_file_name,
        &caption,
        &icon_name,
        &process_id,
        &minimized,
        &maximized,
        &skip_taskbar,
        &x,
        &y,
        &width,
        &height,
        &activities,
        &desktops);

    revision = message_revision;

    window.id = internal_id;
    window.desktop_file_name =
        desktop_file_name;
    window.caption = caption;
    window.icon_name = icon_name;

    window.activity_ids =
        string_array(activities);
    window.desktop_ids =
        string_array(desktops);

    window.frame_geometry.x = x;
    window.frame_geometry.y = y;
    window.frame_geometry.width = width;
    window.frame_geometry.height = height;

    window.process_id = process_id;

    window.minimized = minimized;
    window.maximized = maximized;
    window.skip_taskbar = skip_taskbar;

    g_variant_unref(activities);
    g_variant_unref(desktops);

    return !window.id.empty();
}

void return_accepted(
    GDBusMethodInvocation *invocation,
    bool accepted)
{
    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new(
            "(b)",
            accepted));
}

}

KWinIntegrationService::
    KWinIntegrationService(
        KWinWindowBackend &backend)
    : m_backend(backend)
{
}

KWinIntegrationService::
    ~KWinIntegrationService()
{
    stop();
}

bool KWinIntegrationService::start()
{
    if (m_name_owner_id != 0)
        return true;

    GError *error = nullptr;

    m_introspection =
        g_dbus_node_info_new_for_xml(
            INTROSPECTION_XML,
            &error);

    if (!m_introspection)
    {
        g_warning(
            "Cannot create KWin integration D-Bus interface: %s",
            error
                ? error->message
                : "unknown error");

        g_clear_error(&error);
        return false;
    }

    m_name_owner_id =
        g_bus_own_name(
            G_BUS_TYPE_SESSION,
            KWinIntegrationProtocol::
                SERVICE_NAME,
            G_BUS_NAME_OWNER_FLAGS_NONE,
            &KWinIntegrationService::
                on_bus_acquired,
            &KWinIntegrationService::
                on_name_acquired,
            &KWinIntegrationService::
                on_name_lost,
            this,
            nullptr);

    return m_name_owner_id != 0;
}

void KWinIntegrationService::stop()
{
    clear_sender(true);
    unregister_object();

    if (m_name_owner_id != 0)
    {
        g_bus_unown_name(
            m_name_owner_id);

        m_name_owner_id = 0;
    }

    if (m_connection)
    {
        g_object_unref(m_connection);
        m_connection = nullptr;
    }

    if (m_introspection)
    {
        g_dbus_node_info_unref(
            m_introspection);

        m_introspection = nullptr;
    }

    m_available = false;
}

bool KWinIntegrationService::available() const
{
    return m_available;
}

void KWinIntegrationService::register_object(
    GDBusConnection *connection)
{
    unregister_object();

    if (m_connection)
        g_object_unref(m_connection);

    m_connection =
        G_DBUS_CONNECTION(
            g_object_ref(connection));

    static const GDBusInterfaceVTable
        interface_vtable{
            &KWinIntegrationService::
                on_method_call,
            nullptr,
            nullptr,
            {0}};

    GError *error = nullptr;

    m_object_registration_id =
        g_dbus_connection_register_object(
            m_connection,
            KWinIntegrationProtocol::
                OBJECT_PATH,
            m_introspection->interfaces[0],
            &interface_vtable,
            this,
            nullptr,
            &error);

    if (m_object_registration_id == 0)
    {
        g_warning(
            "Cannot register KWin integration D-Bus object: %s",
            error
                ? error->message
                : "unknown error");
    }

    g_clear_error(&error);
}

void KWinIntegrationService::
    unregister_object()
{
    if (m_connection &&
        m_object_registration_id != 0)
    {
        g_dbus_connection_unregister_object(
            m_connection,
            m_object_registration_id);
    }

    m_object_registration_id = 0;
}

bool KWinIntegrationService::register_sender(
    const char *sender,
    unsigned int protocol_version)
{
    if (!sender ||
        (!m_sender.empty() &&
         m_sender != sender))
    {
        return false;
    }

    if (!m_backend.register_integration(
            protocol_version))
    {
        return false;
    }

    clear_sender(false);
    m_sender = sender;

    m_sender_watch_id =
        g_bus_watch_name_on_connection(
            m_connection,
            m_sender.c_str(),
            G_BUS_NAME_WATCHER_FLAGS_NONE,
            nullptr,
            &KWinIntegrationService::
                on_sender_vanished,
            this,
            nullptr);

    return true;
}

bool KWinIntegrationService::accepts_sender(
    const char *sender) const
{
    return sender &&
           !m_sender.empty() &&
           m_sender == sender;
}

void KWinIntegrationService::clear_sender(
    bool unregister_integration)
{
    if (m_sender_watch_id != 0)
    {
        const auto watch_id =
            m_sender_watch_id;

        m_sender_watch_id = 0;
        g_bus_unwatch_name(watch_id);
    }

    m_sender.clear();

    if (unregister_integration)
        m_backend.unregister_integration();
}

void KWinIntegrationService::
    handle_method_call(
        const char *sender,
        const char *method_name,
        GVariant *parameters,
        GDBusMethodInvocation *invocation)
{
    if (std::strcmp(
            method_name,
            "Ping") == 0)
    {
        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new(
                "(ut)",
                KWinIntegrationProtocol::
                    VERSION,
                m_backend.last_revision()));

        return;
    }

    if (std::strcmp(
            method_name,
            "Register") == 0)
    {
        guint32 protocol_version = 0;

        g_variant_get(
            parameters,
            "(u)",
            &protocol_version);

        const bool accepted =
            register_sender(
                sender,
                protocol_version);

        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new(
                "(bu)",
                accepted,
                KWinIntegrationProtocol::
                    VERSION));

        return;
    }

    if (!accepts_sender(sender))
    {
        g_dbus_method_invocation_return_dbus_error(
            invocation,
            "org.docklight6.Error.NotRegistered",
            "Register the KWin integration before publishing window state");

        return;
    }

    if (std::strcmp(
            method_name,
            "Unregister") == 0)
    {
        clear_sender(true);

        g_dbus_method_invocation_return_value(
            invocation,
            nullptr);

        return;
    }

    if (std::strcmp(
            method_name,
            "BeginSnapshot") == 0)
    {
        guint64 revision = 0;

        g_variant_get(
            parameters,
            "(t)",
            &revision);

        return_accepted(
            invocation,
            m_backend.begin_snapshot(
                revision));

        return;
    }

    if (std::strcmp(
            method_name,
            "StageWindow") == 0 ||
        std::strcmp(
            method_name,
            "PublishWindow") == 0)
    {
        std::uint64_t revision = 0;
        ManagedWindow window;

        const bool parsed =
            parse_window(
                parameters,
                revision,
                window);

        const bool staged =
            std::strcmp(
                method_name,
                "StageWindow") == 0;

        const bool accepted =
            parsed &&
            (staged
                 ? m_backend.stage_window(
                       revision,
                       window)
                 : m_backend.publish_window(
                       revision,
                       window));

        return_accepted(
            invocation,
            accepted);

        return;
    }

    if (std::strcmp(
            method_name,
            "CommitSnapshot") == 0)
    {
        guint64 revision = 0;
        const char *active_window = nullptr;
        GVariant *stacking_order = nullptr;

        g_variant_get(
            parameters,
            "(t&s@as)",
            &revision,
            &active_window,
            &stacking_order);

        const bool accepted =
            m_backend.commit_snapshot(
                revision,
                optional_window_id(
                    active_window),
                string_array(
                    stacking_order));

        g_variant_unref(stacking_order);

        return_accepted(
            invocation,
            accepted);

        return;
    }

    if (std::strcmp(
            method_name,
            "PublishWindowRemoved") == 0)
    {
        guint64 revision = 0;
        const char *window_id = nullptr;

        g_variant_get(
            parameters,
            "(t&s)",
            &revision,
            &window_id);

        return_accepted(
            invocation,
            m_backend
                .publish_window_removed(
                    revision,
                    window_id));

        return;
    }

    if (std::strcmp(
            method_name,
            "PublishActiveWindow") == 0)
    {
        guint64 revision = 0;
        const char *window_id = nullptr;

        g_variant_get(
            parameters,
            "(t&s)",
            &revision,
            &window_id);

        return_accepted(
            invocation,
            m_backend
                .publish_active_window(
                    revision,
                    optional_window_id(
                        window_id)));

        return;
    }

    if (std::strcmp(
            method_name,
            "PublishStackingOrder") == 0)
    {
        guint64 revision = 0;
        GVariant *stacking_order = nullptr;

        g_variant_get(
            parameters,
            "(t@as)",
            &revision,
            &stacking_order);

        const bool accepted =
            m_backend
                .publish_stacking_order(
                    revision,
                    string_array(
                        stacking_order));

        g_variant_unref(stacking_order);

        return_accepted(
            invocation,
            accepted);

        return;
    }

    g_dbus_method_invocation_return_error(
        invocation,
        G_DBUS_ERROR,
        G_DBUS_ERROR_UNKNOWN_METHOD,
        "Unknown KWin integration method: %s",
        method_name);
}

void KWinIntegrationService::on_bus_acquired(
    GDBusConnection *connection,
    const gchar *,
    gpointer user_data)
{
    static_cast<KWinIntegrationService *>(
        user_data)
        ->register_object(connection);
}

void KWinIntegrationService::on_name_acquired(
    GDBusConnection *,
    const gchar *,
    gpointer user_data)
{
    auto service =
        static_cast<KWinIntegrationService *>(
            user_data);

    service->m_available =
        service->m_object_registration_id !=
        0;
}

void KWinIntegrationService::on_name_lost(
    GDBusConnection *,
    const gchar *,
    gpointer user_data)
{
    auto service =
        static_cast<KWinIntegrationService *>(
            user_data);

    service->m_available = false;
    service->clear_sender(true);
}

void KWinIntegrationService::
    on_sender_vanished(
        GDBusConnection *,
        const gchar *name,
        gpointer user_data)
{
    auto service =
        static_cast<KWinIntegrationService *>(
            user_data);

    if (service->m_sender == name)
        service->clear_sender(true);
}

void KWinIntegrationService::on_method_call(
    GDBusConnection *,
    const gchar *sender,
    const gchar *,
    const gchar *,
    const gchar *method_name,
    GVariant *parameters,
    GDBusMethodInvocation *invocation,
    gpointer user_data)
{
    static_cast<KWinIntegrationService *>(
        user_data)
        ->handle_method_call(
            sender,
            method_name,
            parameters,
            invocation);
}
