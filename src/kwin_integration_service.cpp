#include "kwin_integration_service.h"

#include "kwin_integration_protocol.h"
#include "kwin_window_backend.h"

#include <glib.h>

#include <charconv>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <system_error>
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
    "      <arg type='s' direction='in' name='protocol_version'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "      <arg type='s' direction='out' name='supported_version'/>"
    "    </method>"
    "    <method name='Unregister'/>"
    "    <method name='BeginSnapshot'>"
    "      <arg type='s' direction='in' name='revision'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "    </method>"
    "    <method name='StageWindow'>"
    "      <arg type='s' direction='in' name='revision'/>"
    "      <arg type='s' direction='in' name='internal_id'/>"
    "      <arg type='s' direction='in' name='desktop_file_name'/>"
    "      <arg type='s' direction='in' name='caption'/>"
    "      <arg type='s' direction='in' name='icon_name'/>"
    "      <arg type='s' direction='in' name='pid'/>"
    "      <arg type='s' direction='in' name='minimized'/>"
    "      <arg type='s' direction='in' name='maximized'/>"
    "      <arg type='s' direction='in' name='skip_taskbar'/>"
    "      <arg type='s' direction='in' name='x'/>"
    "      <arg type='s' direction='in' name='y'/>"
    "      <arg type='s' direction='in' name='width'/>"
    "      <arg type='s' direction='in' name='height'/>"
    "      <arg type='s' direction='in' name='activities'/>"
    "      <arg type='s' direction='in' name='desktops'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "    </method>"
    "    <method name='CommitSnapshot'>"
    "      <arg type='s' direction='in' name='revision'/>"
    "      <arg type='s' direction='in' name='active_window'/>"
    "      <arg type='s' direction='in' name='stacking_order'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "    </method>"
    "    <method name='PublishWindow'>"
    "      <arg type='s' direction='in' name='revision'/>"
    "      <arg type='s' direction='in' name='internal_id'/>"
    "      <arg type='s' direction='in' name='desktop_file_name'/>"
    "      <arg type='s' direction='in' name='caption'/>"
    "      <arg type='s' direction='in' name='icon_name'/>"
    "      <arg type='s' direction='in' name='pid'/>"
    "      <arg type='s' direction='in' name='minimized'/>"
    "      <arg type='s' direction='in' name='maximized'/>"
    "      <arg type='s' direction='in' name='skip_taskbar'/>"
    "      <arg type='s' direction='in' name='x'/>"
    "      <arg type='s' direction='in' name='y'/>"
    "      <arg type='s' direction='in' name='width'/>"
    "      <arg type='s' direction='in' name='height'/>"
    "      <arg type='s' direction='in' name='activities'/>"
    "      <arg type='s' direction='in' name='desktops'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "    </method>"
    "    <method name='PublishWindowRemoved'>"
    "      <arg type='s' direction='in' name='revision'/>"
    "      <arg type='s' direction='in' name='internal_id'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "    </method>"
    "    <method name='PublishActiveWindow'>"
    "      <arg type='s' direction='in' name='revision'/>"
    "      <arg type='s' direction='in' name='internal_id'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "    </method>"
    "    <method name='PublishStackingOrder'>"
    "      <arg type='s' direction='in' name='revision'/>"
    "      <arg type='s' direction='in' name='stacking_order'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "    </method>"
    "  </interface>"
    "</node>";

const std::string PROTOCOL_VERSION_TEXT =
    std::to_string(
        KWinIntegrationProtocol::VERSION);

template <typename Integer>
bool parse_integer(
    const char *text,
    Integer &value)
{
    if (!text || text[0] == '\0')
        return false;

    const auto end =
        text + std::strlen(text);

    const auto result =
        std::from_chars(
            text,
            end,
            value);

    return result.ec == std::errc{} &&
           result.ptr == end;
}

bool parse_boolean(
    const char *text,
    bool &value)
{
    if (std::strcmp(text, "0") == 0)
    {
        value = false;
        return true;
    }

    if (std::strcmp(text, "1") == 0)
    {
        value = true;
        return true;
    }

    return false;
}

bool parse_string_array(
    const char *encoded_values,
    std::vector<std::string> &values)
{
    values.clear();

    if (!encoded_values ||
        encoded_values[0] == '\0')
    {
        return true;
    }

    auto parts =
        g_strsplit(
            encoded_values,
            ",",
            -1);

    for (int index = 0;
         parts[index];
         ++index)
    {
        auto decoded =
            g_uri_unescape_string(
                parts[index],
                nullptr);

        if (!decoded)
        {
            g_strfreev(parts);
            values.clear();
            return false;
        }

        values.emplace_back(decoded);
        g_free(decoded);
    }

    g_strfreev(parts);

    return true;
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
    const char *revision_text = nullptr;
    const char *internal_id = nullptr;
    const char *desktop_file_name = nullptr;
    const char *caption = nullptr;
    const char *icon_name = nullptr;
    const char *process_id = nullptr;
    const char *minimized = nullptr;
    const char *maximized = nullptr;
    const char *skip_taskbar = nullptr;
    const char *x = nullptr;
    const char *y = nullptr;
    const char *width = nullptr;
    const char *height = nullptr;
    const char *activities = nullptr;
    const char *desktops = nullptr;

    g_variant_get(
        parameters,
        "(&s&s&s&s&s&s&s&s&s&s&s&s&s&s&s)",
        &revision_text,
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

    if (!parse_integer(
            revision_text,
            revision) ||
        !parse_integer(
            process_id,
            window.process_id) ||
        !parse_boolean(
            minimized,
            window.minimized) ||
        !parse_boolean(
            maximized,
            window.maximized) ||
        !parse_boolean(
            skip_taskbar,
            window.skip_taskbar) ||
        !parse_integer(
            x,
            window.frame_geometry.x) ||
        !parse_integer(
            y,
            window.frame_geometry.y) ||
        !parse_integer(
            width,
            window.frame_geometry.width) ||
        !parse_integer(
            height,
            window.frame_geometry.height) ||
        !parse_string_array(
            activities,
            window.activity_ids) ||
        !parse_string_array(
            desktops,
            window.desktop_ids))
    {
        return false;
    }

    window.id = internal_id;
    window.desktop_file_name =
        desktop_file_name;
    window.caption = caption;
    window.icon_name = icon_name;

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
        const char *protocol_version_text =
            nullptr;
        std::uint32_t protocol_version = 0;

        g_variant_get(
            parameters,
            "(&s)",
            &protocol_version_text);

        const bool accepted =
            parse_integer(
                protocol_version_text,
                protocol_version) &&
            register_sender(
                sender,
                protocol_version);

        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new(
                "(bs)",
                accepted,
                PROTOCOL_VERSION_TEXT
                    .c_str()));

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
        const char *revision_text = nullptr;
        std::uint64_t revision = 0;

        g_variant_get(
            parameters,
            "(&s)",
            &revision_text);

        return_accepted(
            invocation,
            parse_integer(
                revision_text,
                revision) &&
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
        const char *revision_text = nullptr;
        const char *active_window = nullptr;
        const char *stacking_order_text =
            nullptr;

        g_variant_get(
            parameters,
            "(&s&s&s)",
            &revision_text,
            &active_window,
            &stacking_order_text);

        std::uint64_t revision = 0;
        std::vector<std::string>
            stacking_order;

        const bool parsed =
            parse_integer(
                revision_text,
                revision) &&
            parse_string_array(
                stacking_order_text,
                stacking_order);

        const bool accepted =
            parsed &&
            m_backend.commit_snapshot(
                revision,
                optional_window_id(
                    active_window),
                stacking_order);

        return_accepted(
            invocation,
            accepted);

        return;
    }

    if (std::strcmp(
            method_name,
            "PublishWindowRemoved") == 0)
    {
        const char *revision_text = nullptr;
        const char *window_id = nullptr;

        g_variant_get(
            parameters,
            "(&s&s)",
            &revision_text,
            &window_id);

        std::uint64_t revision = 0;

        return_accepted(
            invocation,
            parse_integer(
                revision_text,
                revision) &&
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
        const char *revision_text = nullptr;
        const char *window_id = nullptr;

        g_variant_get(
            parameters,
            "(&s&s)",
            &revision_text,
            &window_id);

        std::uint64_t revision = 0;

        return_accepted(
            invocation,
            parse_integer(
                revision_text,
                revision) &&
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
        const char *revision_text = nullptr;
        const char *stacking_order_text =
            nullptr;

        g_variant_get(
            parameters,
            "(&s&s)",
            &revision_text,
            &stacking_order_text);

        std::uint64_t revision = 0;
        std::vector<std::string>
            stacking_order;

        const bool accepted =
            parse_integer(
                revision_text,
                revision) &&
            parse_string_array(
                stacking_order_text,
                stacking_order) &&
                m_backend
                .publish_stacking_order(
                    revision,
                    stacking_order);

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
