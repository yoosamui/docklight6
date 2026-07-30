#include "kwin_integration_protocol.h"
#include "kwin_integration_service.h"
#include "kwin_window_backend.h"

#include <gio/gio.h>
#include <glib.h>

#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace
{

struct AsyncCall
{
    GMainLoop *loop = nullptr;
    GVariant *result = nullptr;
    GError *error = nullptr;

    bool timed_out = false;
};

gboolean on_call_timeout(
    gpointer user_data)
{
    auto call =
        static_cast<AsyncCall *>(
            user_data);

    call->timed_out = true;
    g_main_loop_quit(call->loop);

    return G_SOURCE_REMOVE;
}

void on_call_finished(
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
    auto call =
        static_cast<AsyncCall *>(
            user_data);

    call->result =
        g_dbus_connection_call_finish(
            G_DBUS_CONNECTION(source),
            result,
            &call->error);

    g_main_loop_quit(call->loop);
}

GVariant *call_method(
    GDBusConnection *connection,
    const char *method_name,
    GVariant *parameters)
{
    AsyncCall call;

    call.loop =
        g_main_loop_new(
            nullptr,
            false);

    const auto timeout_id =
        g_timeout_add(
            2000,
            on_call_timeout,
            &call);

    g_dbus_connection_call(
        connection,
        KWinIntegrationProtocol::
            SERVICE_NAME,
        KWinIntegrationProtocol::
            OBJECT_PATH,
        KWinIntegrationProtocol::
            INTERFACE_NAME,
        method_name,
        parameters,
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        2000,
        nullptr,
        on_call_finished,
        &call);

    g_main_loop_run(call.loop);

    if (!call.timed_out)
        g_source_remove(timeout_id);

    g_main_loop_unref(call.loop);

    assert(!call.timed_out);
    assert(!call.error);
    assert(call.result);

    g_clear_error(&call.error);

    return call.result;
}

bool wait_until(
    const std::function<bool()> &condition)
{
    const auto deadline =
        g_get_monotonic_time() +
        2 * G_TIME_SPAN_SECOND;

    while (!condition() &&
           g_get_monotonic_time() <
               deadline)
    {
        while (g_main_context_iteration(
            nullptr,
            false))
        {
        }

        g_usleep(1000);
    }

    return condition();
}

GDBusConnection *connect_to_test_bus(
    GTestDBus *test_bus)
{
    GError *error = nullptr;

    auto connection =
        g_dbus_connection_new_for_address_sync(
            g_test_dbus_get_bus_address(
                test_bus),
            static_cast<
                GDBusConnectionFlags>(
                G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT |
                G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
            nullptr,
            nullptr,
            &error);

    assert(!error);
    assert(connection);

    g_clear_error(&error);

    return connection;
}

GVariant *window_parameters(
    std::uint64_t revision,
    const char *caption)
{
    const auto revision_text =
        std::to_string(revision);
    auto encoded_caption =
        g_uri_escape_string(
            caption,
            nullptr,
            false);

    const std::string payload =
        "window-1,"
        "org.kde.dolphin," +
        std::string(encoded_caption) +
        ",system-file-manager,"
        "1234,0,0,0,10,20,800,600,"
        "activity-1,desktop-1,2,0";

    g_free(encoded_caption);

    return g_variant_new(
        "(ss)",
        revision_text.c_str(),
        payload.c_str());
}

bool accepted(
    GVariant *result)
{
    gboolean value = false;

    g_variant_get(
        result,
        "(b)",
        &value);

    g_variant_unref(result);

    return value;
}

void verifies_dbus_state_transport()
{
    auto test_bus =
        g_test_dbus_new(
            G_TEST_DBUS_NONE);

    g_test_dbus_up(test_bus);

    KWinWindowBackend backend;
    KWinIntegrationService service(
        backend);

    backend.start();

    assert(service.start());
    assert(service.available());

    auto client =
        connect_to_test_bus(test_bus);

    auto result =
        call_method(
            client,
            "Ping",
            nullptr);

    guint32 protocol_version = 0;
    guint64 last_revision = 0;

    g_variant_get(
        result,
        "(ut)",
        &protocol_version,
        &last_revision);

    g_variant_unref(result);

    assert(protocol_version ==
           KWinIntegrationProtocol::VERSION);
    assert(last_revision == 0);

    result =
        call_method(
            client,
            "Register",
            g_variant_new(
                "(s)",
                "2"));

    gboolean registered = true;
    const char *supported_version =
        nullptr;

    g_variant_get(
        result,
        "(b&s)",
        &registered,
        &supported_version);

    const std::string
        supported_version_text =
            supported_version;

    g_variant_unref(result);

    assert(!registered);
    assert(supported_version_text ==
           "5");

    result =
        call_method(
            client,
            "Register",
            g_variant_new(
                "(s)",
                "5"));

    g_variant_get(
        result,
        "(b&s)",
        &registered,
        &supported_version);

    g_variant_unref(result);

    assert(registered);

    assert(accepted(
        call_method(
            client,
            "BeginSnapshot",
            g_variant_new(
                "(s)",
                "10"))));

    assert(accepted(
        call_method(
            client,
            "StageWindow",
            window_parameters(
                10,
                "Home"))));

    assert(accepted(
        call_method(
            client,
            "CommitSnapshot",
            g_variant_new(
                "(sss)",
                "10",
                "window-1",
                "window-1"))));

    assert(backend.connected());
    assert(backend.windows().size() == 1);
    assert(backend.windows()[0].caption ==
           "Home");
    assert(backend.windows()[0]
               .frame_geometry.width ==
           800);
    assert(backend.windows()[0]
               .activity_ids ==
           std::vector<std::string>({
               "activity-1"}));
    assert(backend.windows()[0]
               .desktop_numbers ==
           std::vector<unsigned int>({
               2}));
    assert(!backend.windows()[0]
                .on_current_desktop);

    assert(accepted(
        call_method(
            client,
            "PublishDockSurfaceGeometry",
            g_variant_new(
                "(siiii)",
                "11",
                0,
                331,
                62,
                822))));

    const std::optional<WindowIconGeometry>
        expected_dock_geometry{
            {0, 331, 62, 822}};

    assert(
        backend.dock_surface_geometry() ==
        expected_dock_geometry);

    assert(accepted(
        call_method(
            client,
            "PublishWindow",
            window_parameters(
                12,
                "Downloads"))));

    assert(backend.windows()[0].caption ==
           "Downloads");

    assert(!accepted(
        call_method(
            client,
            "PublishWindow",
            window_parameters(
                12,
                "Stale"))));

    assert(backend.activate_window(
        "window-1"));

    result =
        call_method(
            client,
            "WaitForCommand",
            nullptr);

    const char *command = nullptr;
    const char *window_id = nullptr;
    gboolean command_state = true;

    g_variant_get(
        result,
        "(&s&sb)",
        &command,
        &window_id,
        &command_state);

    assert(std::string(command) ==
           "activate");
    assert(std::string(window_id) ==
           "window-1");
    assert(!command_state);

    g_variant_unref(result);

    assert(backend.set_window_minimized(
        "window-1",
        true));
    assert(backend.set_window_minimized(
        "window-1",
        false));

    result =
        call_method(
            client,
            "WaitForCommand",
            nullptr);

    g_variant_get(
        result,
        "(&s&sb)",
        &command,
        &window_id,
        &command_state);

    assert(std::string(command) ==
           "set-minimized");
    assert(std::string(window_id) ==
           "window-1");
    assert(!command_state);

    g_variant_unref(result);

    auto other_client =
        connect_to_test_bus(test_bus);

    assert(
        backend.set_window_icon_geometry(
            "window-1",
            {100, 200, 64, 64}));

    result =
        call_method(
            other_client,
            "GetIconGeometries",
            nullptr);

    GVariant *geometries = nullptr;

    g_variant_get(
        result,
        "(@a(siiii))",
        &geometries);

    assert(
        g_variant_n_children(
            geometries) == 1);

    const char *geometry_window_id =
        nullptr;
    gint32 geometry_x = 0;
    gint32 geometry_y = 0;
    gint32 geometry_width = 0;
    gint32 geometry_height = 0;

    g_variant_get_child(
        geometries,
        0,
        "(&siiii)",
        &geometry_window_id,
        &geometry_x,
        &geometry_y,
        &geometry_width,
        &geometry_height);

    assert(
        std::string(
            geometry_window_id) ==
        "window-1");
    assert(geometry_x == 100);
    assert(geometry_y == 200);
    assert(geometry_width == 64);
    assert(geometry_height == 64);

    g_variant_unref(geometries);
    g_variant_unref(result);

    result =
        call_method(
            other_client,
            "Register",
            g_variant_new(
                "(s)",
                "3"));

    g_variant_get(
        result,
        "(b&s)",
        &registered,
        &supported_version);

    g_variant_unref(result);

    assert(!registered);

    GError *error = nullptr;

    assert(g_dbus_connection_close_sync(
        other_client,
        nullptr,
        &error));
    assert(!error);

    g_clear_error(&error);
    g_object_unref(other_client);

    assert(g_dbus_connection_close_sync(
        client,
        nullptr,
        &error));
    assert(!error);

    g_clear_error(&error);
    g_object_unref(client);

    assert(wait_until(
        [&backend]()
        {
            return !backend.connected();
        }));

    service.stop();
    backend.stop();

    g_test_dbus_down(test_bus);
    g_object_unref(test_bus);
}

}

int main()
{
    g_setenv(
        "DOCKLIGHT_DISABLE_MINIMIZE_EFFECT_BRIDGE",
        "1",
        true);

    verifies_dbus_state_transport();

    return 0;
}
