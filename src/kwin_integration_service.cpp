// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// kwin_integration_service.cpp
//
// Implementation overview:
// Implements the versioned D-Bus protocol used to exchange KWin window
// state, commands, and compositor-effect geometry.
//
// Important implementation decisions:
// - Only the registered sender may mutate backend state.
// - D-Bus values are validated before crossing into typed backend data.
// - Commands are queued and paired with their pending D-Bus invocations.
// - Geometry updates are coalesced before publishing effect properties.
// - Stop and sender-loss paths release every registration and callback.
//
// ------------------------------------------------------------

#include "kwin_integration_service.h"

#include "kwin_integration_protocol.h"
#include "kwin_window_backend.h"

#include <glib.h>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{

constexpr char INTROSPECTION_XML[] = // D-Bus interface introspection document
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
    "    <method name='GetIconGeometries'>"
    "      <arg type='a(siiii)' direction='out' name='geometries'/>"
    "    </method>"
    "    <method name='GetDockSurfaceGeometry'>"
    "      <arg type='b' direction='out' name='available'/>"
    "      <arg type='i' direction='out' name='x'/>"
    "      <arg type='i' direction='out' name='y'/>"
    "      <arg type='i' direction='out' name='width'/>"
    "      <arg type='i' direction='out' name='height'/>"
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
"    <method name='PublishCurrentDesktop'>"
"      <arg type='s' direction='in' name='revision'/>"
"      <arg type='s' direction='in' name='desktop_id'/>"
"      <arg type='i' direction='in' name='desktop_number'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "    </method>"
    "    <method name='PublishDockSurfaceGeometry'>"
    "      <arg type='s' direction='in' name='revision'/>"
    "      <arg type='i' direction='in' name='x'/>"
    "      <arg type='i' direction='in' name='y'/>"
    "      <arg type='i' direction='in' name='width'/>"
    "      <arg type='i' direction='in' name='height'/>"
    "      <arg type='b' direction='out' name='accepted'/>"
    "    </method>"
    "    <signal name='IconGeometryChanged'>"
    "      <arg type='s' name='internal_id'/>"
    "      <arg type='i' name='x'/>"
    "      <arg type='i' name='y'/>"
    "      <arg type='i' name='width'/>"
    "      <arg type='i' name='height'/>"
    "    </signal>"
    "    <signal name='IconGeometryRemoved'>"
    "      <arg type='s' name='internal_id'/>"
    "    </signal>"
    "    <signal name='DockSurfaceGeometryChanged'>"
    "      <arg type='b' name='available'/>"
    "      <arg type='i' name='x'/>"
    "      <arg type='i' name='y'/>"
    "      <arg type='i' name='width'/>"
    "      <arg type='i' name='height'/>"
    "    </signal>"
    "  </interface>"
    "</node>";

const std::string PROTOCOL_VERSION_TEXT =
    std::to_string(
        KWinIntegrationProtocol::VERSION); // Protocol version formatted for string messages

constexpr guint32 DBUS_NAME_FLAG_DO_NOT_QUEUE =
    4; // D-Bus flag that rejects queued name ownership
constexpr guint32 DBUS_REQUEST_NAME_PRIMARY_OWNER =
    1; // D-Bus result for newly acquired ownership
constexpr guint32 DBUS_REQUEST_NAME_ALREADY_OWNER =
    4; // D-Bus result for existing ownership
constexpr char MINIMIZE_EFFECT_ID[] =
    "org.docklight6.minimize"; // DockLight KWin minimize-effect ID
constexpr char MINIMIZE_EFFECT_GROUP[] =
    "Effect-org.docklight6.minimize"; // KWin configuration group for the effect
constexpr guint EFFECT_GEOMETRY_UPDATE_DELAY_MS =
    50; // Delay used to coalesce effect geometry updates

bool minimize_effect_is_installed()
{
    if (g_getenv(
            "DOCKLIGHT_DISABLE_MINIMIZE_EFFECT_BRIDGE"))
    {
        return false;
    }

    auto metadata_path =
        g_build_filename(
            g_get_user_data_dir(),
            "kwin",
            "effects",
            MINIMIZE_EFFECT_ID,
            "metadata.json",
            nullptr);

    const bool installed =
        g_file_test(
            metadata_path,
            G_FILE_TEST_IS_REGULAR);

    g_free(metadata_path);

    return installed;
}

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

std::string encode_string_array(
    const std::vector<std::string>
        &values)
{
    std::ostringstream encoded;

    for (auto value = values.begin();
         value != values.end();
         ++value)
    {
        if (value != values.begin())
            encoded << ',';

        auto escaped =
            g_uri_escape_string(
                value->c_str(),
                nullptr,
                true);

        encoded << escaped;
        g_free(escaped);
    }

    return encoded.str();
}

bool parse_desktop_numbers(
    const char *encoded_values,
    std::vector<unsigned int> &numbers)
{
    std::vector<std::string> values;

    if (!parse_string_array(
            encoded_values,
            values))
    {
        return false;
    }

    numbers.clear();
    numbers.reserve(values.size());

    for (const auto &value : values)
    {
        unsigned int number = 0;

        if (!parse_integer(
                value.c_str(),
                number) ||
            number == 0)
        {
            numbers.clear();
            return false;
        }

        numbers.push_back(number);
    }

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
        fields.size() != 16)
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
            window.desktop_ids) ||
        !parse_desktop_numbers(
            fields[14].c_str(),
            window.desktop_numbers) ||
        !parse_boolean(
            fields[15].c_str(),
            window.on_current_desktop))
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

    m_backend.set_icon_geometry_handler(
        [this](
            const WindowId &window_id,
            const WindowIconGeometry
                &geometry)
        {
            return publish_icon_geometry(
                window_id,
                geometry);
        });

    m_window_removed =
        m_backend
            .signal_window_removed()
            .connect(
                sigc::mem_fun(
                    *this,
                    &KWinIntegrationService::
                        remove_icon_geometry));

    m_snapshot_changed =
        m_backend
            .signal_snapshot_changed()
            .connect(
                sigc::mem_fun(
                    *this,
                    &KWinIntegrationService::
                        prune_icon_geometries));

    m_connection_changed =
        m_backend
            .signal_connection_changed()
            .connect(
                sigc::mem_fun(
                    *this,
                    &KWinIntegrationService::
                        handle_backend_connection));

    m_dock_surface_geometry_changed =
        m_backend
            .signal_dock_surface_geometry_changed()
            .connect(
                sigc::mem_fun(
                    *this,
                    &KWinIntegrationService::
                        emit_dock_surface_geometry));
}

KWinIntegrationService::
    ~KWinIntegrationService()
{
    stop();
    m_backend.set_command_handler({});
    m_backend.set_icon_geometry_handler(
        {});
    m_window_removed.disconnect();
    m_snapshot_changed.disconnect();
    m_connection_changed.disconnect();
    m_dock_surface_geometry_changed
        .disconnect();
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
    schedule_effect_geometry_update();

    return true;
}

void KWinIntegrationService::stop()
{
    cancel_effect_geometry_update();

    if (m_effect_geometries_initialized &&
        !m_published_effect_geometries.empty())
    {
        write_effect_geometries({});
    }

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
        64; // Maximum queued KWin commands

    if (m_sender.empty())
        return false;

    const auto pending =
        std::find_if(
            m_commands.begin(),
            m_commands.end(),
            [&command](
                const KWinWindowCommand
                    &candidate)
            {
                return candidate.type ==
                           command.type &&
                       candidate.window_id ==
                           command.window_id;
            });

    if (pending != m_commands.end())
    {
        // Keep only the newest requested state for each window. Rapid clicks
        // can otherwise enqueue the same group action repeatedly while KWin
        // is still animating the first request, producing seconds of stale
        // work and making the desktop appear frozen.
        *pending = command;
        return true;
    }

    if (m_commands.size() >=
        maximum_commands)
    {
        return false;
    }

    m_commands.push_back(command);
    deliver_next_command();

    return true;
}

bool KWinIntegrationService::
    publish_icon_geometry(
        const WindowId &window_id,
        const WindowIconGeometry &geometry)
{
    if (!m_available ||
        !m_connection ||
        window_id.empty() ||
        geometry.width <= 0 ||
        geometry.height <= 0)
    {
        return false;
    }

    const auto current =
        m_icon_geometries.find(
            window_id);

    if (current !=
            m_icon_geometries.end() &&
        current->second == geometry)
    {
        return true;
    }

    m_icon_geometries[window_id] =
        geometry;
    schedule_effect_geometry_update();

    GError *error = nullptr;

    const bool emitted =
        g_dbus_connection_emit_signal(
            m_connection,
            nullptr,
            KWinIntegrationProtocol::
                OBJECT_PATH,
            KWinIntegrationProtocol::
                INTERFACE_NAME,
            "IconGeometryChanged",
            g_variant_new(
                "(siiii)",
                window_id.c_str(),
                geometry.x,
                geometry.y,
                geometry.width,
                geometry.height),
            &error);

    if (!emitted)
    {
        g_warning(
            "Cannot publish KWin icon geometry: %s",
            error
                ? error->message
                : "unknown error");
    }

    g_clear_error(&error);

    return emitted;
}

void KWinIntegrationService::
    remove_icon_geometry(
        const WindowId &window_id)
{
    if (m_icon_geometries.erase(
            window_id) == 0)
    {
        return;
    }

    emit_icon_geometry_removed(
        window_id);
    schedule_effect_geometry_update();
}

void KWinIntegrationService::
    handle_backend_connection(
        bool connected)
{
    if (connected)
    {
        prune_icon_geometries();
        return;
    }

    while (!m_icon_geometries.empty())
    {
        const auto window_id =
            m_icon_geometries.begin()
                ->first;

        m_icon_geometries.erase(
            m_icon_geometries.begin());

        emit_icon_geometry_removed(
            window_id);
    }

    schedule_effect_geometry_update();
}

void KWinIntegrationService::
    prune_icon_geometries()
{
    const auto windows =
        m_backend.windows();
    bool changed = false;

    for (auto iterator =
             m_icon_geometries.begin();
         iterator !=
             m_icon_geometries.end();)
    {
        const auto found =
            std::find_if(
                windows.begin(),
                windows.end(),
                [&iterator](
                    const ManagedWindow
                        &window)
                {
                    return window.id ==
                           iterator->first;
                });

        if (found != windows.end())
        {
            ++iterator;
            continue;
        }

        const auto window_id =
            iterator->first;

        iterator =
            m_icon_geometries.erase(
                iterator);
        changed = true;

        emit_icon_geometry_removed(
            window_id);
    }

    if (changed)
        schedule_effect_geometry_update();
}

void KWinIntegrationService::
    schedule_effect_geometry_update()
{
    if (m_effect_geometry_update_id != 0 ||
        !minimize_effect_is_installed())
    {
        return;
    }

    m_effect_geometry_update_id =
        g_timeout_add(
            EFFECT_GEOMETRY_UPDATE_DELAY_MS,
            &KWinIntegrationService::
                on_effect_geometry_update,
            this);
}

void KWinIntegrationService::
    publish_effect_geometries()
{
    std::map<
        std::int64_t,
        WindowIconGeometry>
        process_geometries;

    for (const auto &window :
         m_backend.windows())
    {
        if (window.process_id <= 0)
            continue;

        const auto geometry =
            m_icon_geometries.find(
                window.id);

        if (geometry ==
            m_icon_geometries.end())
        {
            continue;
        }

        process_geometries[
            window.process_id] =
            geometry->second;
    }

    std::ostringstream encoded;
    bool first = true;

    for (const auto &entry :
         process_geometries)
    {
        if (!first)
            encoded << ';';

        first = false;

        encoded
            << entry.first << ','
            << entry.second.x << ','
            << entry.second.y << ','
            << entry.second.width << ','
            << entry.second.height;
    }

    write_effect_geometries(
        encoded.str());
}

bool KWinIntegrationService::
    write_effect_geometries(
        const std::string &geometries)
{
    if ((m_effect_geometries_initialized &&
         geometries ==
             m_published_effect_geometries) ||
        !minimize_effect_is_installed())
    {
        return true;
    }

    gchar *arguments[] = {
        const_cast<gchar *>(
            "kwriteconfig6"),
        const_cast<gchar *>("--file"),
        const_cast<gchar *>("kwinrc"),
        const_cast<gchar *>("--group"),
        const_cast<gchar *>(
            MINIMIZE_EFFECT_GROUP),
        const_cast<gchar *>("--key"),
        const_cast<gchar *>("Geometries"),
        const_cast<gchar *>(
            geometries.c_str()),
        nullptr};

    GError *error = nullptr;
    gint exit_status = 0;

    const bool spawned =
        g_spawn_sync(
            nullptr,
            arguments,
            nullptr,
            G_SPAWN_SEARCH_PATH,
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            &exit_status,
            &error);

    if (!spawned ||
        !g_spawn_check_wait_status(
            exit_status,
            &error))
    {
        g_warning(
            "Cannot publish Docklight minimize-effect geometries: %s",
            error
                ? error->message
                : "unknown error");

        g_clear_error(&error);
        return false;
    }

    m_published_effect_geometries =
        geometries;
    m_effect_geometries_initialized =
        true;

    if (m_connection)
    {
        g_dbus_connection_call(
            m_connection,
            "org.kde.KWin",
            "/Effects",
            "org.kde.kwin.Effects",
            "reconfigureEffect",
            g_variant_new(
                "(s)",
                MINIMIZE_EFFECT_ID),
            nullptr,
            G_DBUS_CALL_FLAGS_NONE,
            -1,
            nullptr,
            nullptr,
            nullptr);
    }

    return true;
}

void KWinIntegrationService::
    cancel_effect_geometry_update()
{
    if (m_effect_geometry_update_id == 0)
        return;

    g_source_remove(
        m_effect_geometry_update_id);
    m_effect_geometry_update_id = 0;
}

void KWinIntegrationService::
    emit_icon_geometry_removed(
        const WindowId &window_id)
{
    if (!m_available ||
        !m_connection)
    {
        return;
    }

    GError *error = nullptr;

    g_dbus_connection_emit_signal(
        m_connection,
        nullptr,
        KWinIntegrationProtocol::
            OBJECT_PATH,
        KWinIntegrationProtocol::
            INTERFACE_NAME,
        "IconGeometryRemoved",
        g_variant_new(
            "(s)",
            window_id.c_str()),
        &error);

    if (error)
    {
        g_warning(
            "Cannot remove KWin icon geometry: %s",
            error->message);
    }

    g_clear_error(&error);
}

void KWinIntegrationService::
    return_icon_geometries(
        GDBusMethodInvocation *invocation) const
{
    GVariantBuilder builder;

    g_variant_builder_init(
        &builder,
        G_VARIANT_TYPE("a(siiii)"));

    for (const auto &entry :
         m_icon_geometries)
    {
        const auto &geometry =
            entry.second;

        g_variant_builder_add(
            &builder,
            "(siiii)",
            entry.first.c_str(),
            geometry.x,
            geometry.y,
            geometry.width,
            geometry.height);
    }

    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new(
            "(a(siiii))",
            &builder));
}

void KWinIntegrationService::
    return_dock_surface_geometry(
        GDBusMethodInvocation *invocation) const
{
    const auto geometry =
        m_backend.dock_surface_geometry();

    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new(
            "(biiii)",
            geometry.has_value(),
            geometry
                ? geometry->x
                : 0,
            geometry
                ? geometry->y
                : 0,
            geometry
                ? geometry->width
                : 0,
            geometry
                ? geometry->height
                : 0));
}

void KWinIntegrationService::
    emit_dock_surface_geometry()
{
    if (!m_available ||
        !m_connection)
    {
        return;
    }

    const auto geometry =
        m_backend.dock_surface_geometry();
    GError *error = nullptr;

    g_dbus_connection_emit_signal(
        m_connection,
        nullptr,
        KWinIntegrationProtocol::
            OBJECT_PATH,
        KWinIntegrationProtocol::
            INTERFACE_NAME,
        "DockSurfaceGeometryChanged",
        g_variant_new(
            "(biiii)",
            geometry.has_value(),
            geometry
                ? geometry->x
                : 0,
            geometry
                ? geometry->y
                : 0,
            geometry
                ? geometry->width
                : 0,
            geometry
                ? geometry->height
                : 0),
        &error);

    if (error)
    {
        g_warning(
            "Cannot publish Docklight surface geometry: %s",
            error->message);
    }

    g_clear_error(&error);
}

void KWinIntegrationService::
    deliver_next_command()
{
    if (m_command_invocations.empty() ||
        m_commands.empty())
    {
        return;
    }

    const auto command =
        m_commands.front();
    m_commands.pop_front();

    const char *command_name = "";
    std::string identifier =
        command.window_id;

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
    case KWinWindowCommandType::PRESENT:
        command_name = "present";
        identifier =
            encode_string_array(
                command.window_ids);
        break;
    case KWinWindowCommandType::HIDE:
        command_name = "hide";
        identifier =
            encode_string_array(
                command.window_ids);
        break;
    }

    auto invocation =
        m_command_invocations.front();
    m_command_invocations.pop_front();

    if (m_command_invocations.empty())
        cancel_command_keepalive();

    g_message(
        "KWin window command delivered: %s %s",
        command_name,
        identifier.c_str());

    g_dbus_method_invocation_return_value(
        invocation,
        g_variant_new(
            "(ssb)",
            command_name,
            identifier.c_str(),
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

    while (!m_command_invocations.empty())
    {
        auto invocation =
            m_command_invocations.front();
        m_command_invocations.pop_front();

        g_dbus_method_invocation_return_dbus_error(
            invocation,
            "org.docklight6.Error.Disconnected",
            "The KWin integration disconnected");

        g_object_unref(invocation);
    }
}

// Validates and dispatches one D-Bus method at the transport boundary.
// Backend state is changed only after sender, protocol, and value checks
// succeed, keeping untrusted variant parsing out of the state model.
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

    if (std::strcmp(
            method_name,
            "GetIconGeometries") == 0)
    {
        g_message(
            "Docklight Plasma geometry bridge connected");

        return_icon_geometries(
            invocation);
        return;
    }

    if (std::strcmp(
            method_name,
            "GetDockSurfaceGeometry") == 0)
    {
        return_dock_surface_geometry(
            invocation);
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
        constexpr std::size_t
            maximum_command_waits = 2;

        if (m_command_invocations.size() >=
            maximum_command_waits)
        {
            g_dbus_method_invocation_return_dbus_error(
                invocation,
                "org.docklight6.Error.AlreadyWaiting",
                "The KWin integration already has two pending command requests");
            return;
        }

        m_command_invocations.push_back(
            G_DBUS_METHOD_INVOCATION(
                g_object_ref(invocation)));

        if (m_command_keepalive_id == 0)
        {
            m_command_keepalive_id =
                g_timeout_add_seconds(
                    20,
                    &KWinIntegrationService::
                        on_command_keepalive,
                    this);
        }

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

    if (std::strcmp(
            method_name,
            "PublishCurrentDesktop") == 0)
    {
        const char *revision_text = nullptr;
        const char *desktop_id = nullptr;
        gint desktop_number = 0;

        g_variant_get(
            parameters,
            "(&s&si)",
            &revision_text,
            &desktop_id,
            &desktop_number);

        std::uint64_t revision = 0;

        return_accepted(
            invocation,
            parse_integer(
                revision_text,
                revision) &&
                m_backend
                    .publish_current_desktop(
                        revision,
                        desktop_id,
                        desktop_number > 0
                            ? static_cast<unsigned int>(
                                  desktop_number)
                            : 0));

        return;
    }

    if (std::strcmp(
            method_name,
            "PublishDockSurfaceGeometry") == 0)
    {
        const char *revision_text = nullptr;
        std::uint64_t revision = 0;
        WindowIconGeometry geometry;

        g_variant_get(
            parameters,
            "(&siiii)",
            &revision_text,
            &geometry.x,
            &geometry.y,
            &geometry.width,
            &geometry.height);

        const bool empty =
            geometry.width == 0 &&
            geometry.height == 0;

        const bool valid =
            geometry.width > 0 &&
            geometry.height > 0;

        return_accepted(
            invocation,
            parse_integer(
                revision_text,
                revision) &&
                (empty || valid) &&
                m_backend
                    .publish_dock_surface_geometry(
                        revision,
                        empty
                            ? std::nullopt
                            : std::optional<
                                  WindowIconGeometry>{
                                  geometry}));

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

    if (service
            ->m_command_invocations
            .empty())
    {
        return G_SOURCE_REMOVE;
    }

    while (!service
                ->m_command_invocations
                .empty())
    {
        auto invocation =
            service
                ->m_command_invocations
                .front();
        service
            ->m_command_invocations
            .pop_front();

        g_dbus_method_invocation_return_value(
            invocation,
            g_variant_new(
                "(ssb)",
                "none",
                "",
                false));

        g_object_unref(invocation);
    }

    return G_SOURCE_REMOVE;
}

gboolean KWinIntegrationService::
    on_effect_geometry_update(
        gpointer user_data)
{
    auto service =
        static_cast<KWinIntegrationService *>(
            user_data);

    service->m_effect_geometry_update_id = 0;
    service->publish_effect_geometries();

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
