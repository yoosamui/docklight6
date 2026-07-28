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
    "    <method name='WaitForCommand'>"
    "      <arg type='s' direction='out' name='command'/>"
    "      <arg type='s' direction='out' name='internal_id'/>"
    "      <arg type='b' direction='out' name='state'/>"
    "    </method>"
    "    <method name='BeginSnapshot'>"
    "      <arg type='s' direction='in' name='revision'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "    </method>"
    "    <method name='StageWindow'>"
    "      <arg type='s' direction='in' name='revision'/>"
    "      <arg type='s' direction='in' name='window_payload'/>"
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
    "      <arg type='s' direction='in' name='window_payload'/>"
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

constexpr guint32 DBUS_NAME_FLAG_DO_NOT_QUEUE =
    4;
constexpr guint32 DBUS_REQUEST_NAME_PRIMARY_OWNER =
    1;
constexpr guint32 DBUS_REQUEST_NAME_ALREADY_OWNER =
    4;

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
    const char *window_payload = nullptr;

    g_variant_get(
        parameters,
        "(&s&s)",
        &revision_text,
        &window_payload);

    std::vector<std::string> fields;

    if (!parse_string_array(
            window_payload,
            fields) ||
        fields.size() != 14)
    {
        return false;
    }

    if (!parse_integer(
            revision_text,
            revision) ||
        !parse_integer(
            fields[4].c_str(),
            window.process_id) ||
        !parse_boolean(
            fields[5].c_str(),
            window.minimized) ||
        !parse_boolean(
            fields[6].c_str(),
            window.maximized) ||
        !parse_boolean(
            fields[7].c_str(),
            window.skip_taskbar) ||
        !parse_integer(
            fields[8].c_str(),
            window.frame_geometry.x) ||
        !parse_integer(
            fields[9].c_str(),
            window.frame_geometry.y) ||
        !parse_integer(
            fields[10].c_str(),
            window.frame_geometry.width) ||
        !parse_integer(
            fields[11].c_str(),
            window.frame_geometry.height) ||
        !parse_string_array(
            fields[12].c_str(),
            window.activity_ids) ||
        !parse_string_array(
            fields[13].c_str(),
            window.desktop_ids))
    {
        return false;
    }

    window.id = fields[0];
    window.desktop_file_name =
        fields[1];
    window.caption = fields[2];
    window.icon_name = fields[3];

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
    m_backend.set_command_handler(
        [this](
            const KWinWindowCommand
                &command)
        {
            return enqueue_command(
                command);
        });
}

KWinIntegrationService::
    ~KWinIntegrationService()
{
    stop();
    m_backend.set_command_handler({});
}

bool KWinIntegrationService::start()
{
    if (m_available)
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

    m_connection =
        g_bus_get_sync(
            G_BUS_TYPE_SESSION,
            nullptr,
            &error);

    if (!m_connection)
    {
        g_warning(
            "Cannot connect to the session D-Bus for KWin integration: %s",
            error
                ? error->message
                : "unknown error");

        g_clear_error(&error);
        stop();
        return false;
    }

    if (!register_object() ||
        !request_name())
    {
        stop();
        return false;
    }

    m_available = true;

    return true;
}

void KWinIntegrationService::stop()
{
    clear_sender(true);
    release_name();
    unregister_object();

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

bool KWinIntegrationService::request_name()
{
    GError *error = nullptr;

    auto result =
        g_dbus_connection_call_sync(
            m_connection,
            "org.freedesktop.DBus",
            "/org/freedesktop/DBus",
            "org.freedesktop.DBus",
            "RequestName",
            g_variant_new(
                "(su)",
                KWinIntegrationProtocol::
                    SERVICE_NAME,
                DBUS_NAME_FLAG_DO_NOT_QUEUE),
            G_VARIANT_TYPE("(u)"),
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            nullptr,
            &error);

    if (!result)
    {
        g_warning(
            "Cannot acquire the KWin integration D-Bus name: %s",
            error
                ? error->message
                : "unknown error");

        g_clear_error(&error);
        return false;
    }

    guint32 reply = 0;
    g_variant_get(
        result,
        "(u)",
        &reply);
    g_variant_unref(result);

    m_name_owned =
        reply ==
            DBUS_REQUEST_NAME_PRIMARY_OWNER ||
        reply ==
            DBUS_REQUEST_NAME_ALREADY_OWNER;

    if (!m_name_owned)
    {
        g_warning(
            "Cannot acquire the KWin integration D-Bus name because another Docklight instance owns it");
    }

    return m_name_owned;
}

void KWinIntegrationService::release_name()
{
    if (!m_connection ||
        !m_name_owned)
    {
        return;
    }

    GError *error = nullptr;

    auto result =
        g_dbus_connection_call_sync(
            m_connection,
            "org.freedesktop.DBus",
            "/org/freedesktop/DBus",
            "org.freedesktop.DBus",
            "ReleaseName",
            g_variant_new(
                "(s)",
                KWinIntegrationProtocol::
                    SERVICE_NAME),
            G_VARIANT_TYPE("(u)"),
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            nullptr,
            &error);

    if (!result)
    {
        g_warning(
            "Cannot release the KWin integration D-Bus name: %s",
            error
                ? error->message
                : "unknown error");
    }
    else
    {
        g_variant_unref(result);
    }

    g_clear_error(&error);
    m_name_owned = false;
}

bool KWinIntegrationService::register_object()
{
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

    return m_object_registration_id != 0;
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
    clear_commands();

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

bool KWinIntegrationService::enqueue_command(
    const KWinWindowCommand &command)
{
    constexpr std::size_t maximum_commands =
        64;

    if (m_sender.empty() ||
        m_commands.size() >=
            maximum_commands)
    {
        return false;
    }

    m_commands.push_back(command);
    deliver_next_command();

    return true;
}

void KWinIntegrationService::
    deliver_next_command()
{
    if (!m_command_invocation ||
        m_commands.empty())
    {
        return;
    }

    const auto command =
        m_commands.front();
    m_commands.pop_front();

    const char *command_name = "";

    switch (command.type)
    {
    case KWinWindowCommandType::ACTIVATE:
        command_name = "activate";
        break;
    case KWinWindowCommandType::RAISE:
        command_name = "raise";
        break;
    case KWinWindowCommandType::CLOSE:
        command_name = "close";
        break;
    case KWinWindowCommandType::
        SET_MINIMIZED:
        command_name = "set-minimized";
        break;
    case KWinWindowCommandType::
        SET_MAXIMIZED:
        command_name = "set-maximized";
        break;
    }

    auto invocation =
        m_command_invocation;

    m_command_invocation = nullptr;
    cancel_command_keepalive();

    g_message(
        "KWin window command delivered: %s %s",
        command_name,
        command.window_id.c_str());

    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new(
            "(ssb)",
            command_name,
            command.window_id.c_str(),
            command.state));

    g_object_unref(invocation);
}

void KWinIntegrationService::
    cancel_command_keepalive()
{
    if (m_command_keepalive_id == 0)
        return;

    const auto keepalive_id =
        m_command_keepalive_id;

    m_command_keepalive_id = 0;
    g_source_remove(keepalive_id);
}

void KWinIntegrationService::clear_commands()
{
    m_commands.clear();
    cancel_command_keepalive();

    if (!m_command_invocation)
        return;

    auto invocation =
        m_command_invocation;

    m_command_invocation = nullptr;

    g_dbus_method_invocation_return_dbus_error(
        invocation,
        "org.docklight6.Error.Disconnected",
        "The KWin integration disconnected");

    g_object_unref(invocation);
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
            "WaitForCommand") == 0)
    {
        if (m_command_invocation)
        {
            g_dbus_method_invocation_return_dbus_error(
                invocation,
                "org.docklight6.Error.AlreadyWaiting",
                "The KWin integration already has a pending command request");
            return;
        }

        m_command_invocation =
            G_DBUS_METHOD_INVOCATION(
                g_object_ref(invocation));

        m_command_keepalive_id =
            g_timeout_add_seconds(
                20,
                &KWinIntegrationService::
                    on_command_keepalive,
                this);

        deliver_next_command();
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

gboolean KWinIntegrationService::
    on_command_keepalive(
        gpointer user_data)
{
    auto service =
        static_cast<KWinIntegrationService *>(
            user_data);

    service->m_command_keepalive_id = 0;

    if (!service->m_command_invocation)
        return G_SOURCE_REMOVE;

    auto invocation =
        service->m_command_invocation;

    service->m_command_invocation = nullptr;

    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new(
            "(ssb)",
            "none",
            "",
            false));

    g_object_unref(invocation);

    return G_SOURCE_REMOVE;
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
