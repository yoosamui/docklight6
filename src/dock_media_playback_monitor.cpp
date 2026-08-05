// MPRIS playback discovery for animated window previews.

#include "dock_media_playback_monitor.h"

#include <glib.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <utility>

namespace
{

constexpr char DBUS_SERVICE[] = "org.freedesktop.DBus";
constexpr char DBUS_PATH[] = "/org/freedesktop/DBus";
constexpr char DBUS_INTERFACE[] = "org.freedesktop.DBus";
constexpr char MPRIS_PREFIX[] = "org.mpris.MediaPlayer2.";
constexpr char MPRIS_PATH[] = "/org/mpris/MediaPlayer2";
constexpr char PROPERTIES_INTERFACE[] =
    "org.freedesktop.DBus.Properties";
constexpr char MPRIS_ROOT_INTERFACE[] =
    "org.mpris.MediaPlayer2";
constexpr char MPRIS_PLAYER_INTERFACE[] =
    "org.mpris.MediaPlayer2.Player";

bool is_mpris_name(const char *name)
{
    return name &&
           g_str_has_prefix(name, MPRIS_PREFIX);
}

std::string normalized_identifier(
    const std::string &value)
{
    auto identifier = value;
    const auto slash =
        identifier.find_last_of("/\\");

    if (slash != std::string::npos)
        identifier.erase(0, slash + 1);

    std::transform(
        identifier.begin(),
        identifier.end(),
        identifier.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(
                std::tolower(character));
        });

    constexpr char DESKTOP_SUFFIX[] = ".desktop";
    if (identifier.size() >=
            sizeof(DESKTOP_SUFFIX) - 1 &&
        identifier.compare(
            identifier.size() -
                (sizeof(DESKTOP_SUFFIX) - 1),
            sizeof(DESKTOP_SUFFIX) - 1,
            DESKTOP_SUFFIX) == 0)
    {
        identifier.erase(
            identifier.size() -
            (sizeof(DESKTOP_SUFFIX) - 1));
    }

    return identifier;
}

std::string final_component(
    const std::string &identifier)
{
    const auto dot = identifier.find_last_of('.');
    return dot == std::string::npos
               ? identifier
               : identifier.substr(dot + 1);
}

std::string application_identifier(
    const std::string &value)
{
    auto identifier = normalized_identifier(value);

    // Browser MPRIS service names append a per-process instance component,
    // for example "chromium.instance20928" or "firefox.instance_1_63".
    // It is not part of the application identity.
    const auto instance = identifier.find(".instance");

    if (instance != std::string::npos)
        identifier.erase(instance);

    return identifier;
}

std::string browser_family(
    const std::string &value)
{
    const auto identifier =
        application_identifier(value);
    const auto component =
        final_component(identifier);

    if (identifier == "google-chrome" ||
        identifier == "google-chrome-stable" ||
        identifier == "chromium" ||
        identifier == "chromium-browser" ||
        component == "chrome" ||
        component == "chromium")
    {
        return "chromium";
    }

    if (identifier == "firefox" ||
        identifier == "firefox-esr" ||
        component == "firefox")
    {
        return "firefox";
    }

    return {};
}

bool identifiers_match(
    const std::string &left,
    const std::string &right)
{
    const auto normalized_left =
        application_identifier(left);
    const auto normalized_right =
        application_identifier(right);

    if (normalized_left.empty() ||
        normalized_right.empty())
    {
        return false;
    }

    if (normalized_left == normalized_right ||
        final_component(normalized_left) ==
            final_component(normalized_right))
    {
        return true;
    }

    const auto left_browser = browser_family(left);
    const auto right_browser = browser_family(right);

    return !left_browser.empty() &&
           left_browser == right_browser;
}

struct PropertyRequest
{
    std::weak_ptr<
        DockMediaPlaybackMonitor::State>
        state;
    std::string service;
    bool player_properties = false;
};

}

struct DockMediaPlaybackMonitor::State
{
    struct Player
    {
        std::string desktop_entry;
        std::string title;
        bool playing = false;
        bool player_request_in_flight = false;
        guint properties_subscription = 0;
    };

    GDBusConnection *connection = nullptr;
    guint names_subscription = 0;
    std::map<std::string, Player> players;
    sigc::signal<void> changed;
    bool alive = true;
};

namespace
{

void request_properties(
    const std::shared_ptr<
        DockMediaPlaybackMonitor::State> &state,
    const std::string &service,
    bool player_properties);

void properties_ready(
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
    std::unique_ptr<PropertyRequest> request(
        static_cast<PropertyRequest *>(user_data));
    const auto state = request->state.lock();

    GError *error = nullptr;
    auto reply = g_dbus_connection_call_finish(
        G_DBUS_CONNECTION(source),
        result,
        &error);

    if (!state || !state->alive)
    {
        if (reply)
            g_variant_unref(reply);
        g_clear_error(&error);
        return;
    }

    const auto player =
        state->players.find(request->service);

    if (player != state->players.end() &&
        request->player_properties)
    {
        player->second.player_request_in_flight =
            false;
    }

    if (!reply || player == state->players.end())
    {
        if (reply)
            g_variant_unref(reply);
        g_clear_error(&error);
        return;
    }

    GVariant *properties = nullptr;
    g_variant_get(reply, "(@a{sv})", &properties);
    bool materially_changed = false;

    if (request->player_properties)
    {
        auto value = g_variant_lookup_value(
            properties,
            "PlaybackStatus",
            G_VARIANT_TYPE_STRING);

        if (value)
        {
            const bool playing =
                std::string{
                    g_variant_get_string(value, nullptr)} ==
                "Playing";
            materially_changed =
                player->second.playing != playing;
            player->second.playing = playing;
            g_variant_unref(value);
        }

        value = g_variant_lookup_value(
            properties,
            "Metadata",
            G_VARIANT_TYPE("a{sv}"));

        if (value)
        {
            auto title_value = g_variant_lookup_value(
                value,
                "xesam:title",
                G_VARIANT_TYPE_STRING);

            if (title_value)
            {
                const std::string title =
                    g_variant_get_string(title_value, nullptr);
                materially_changed =
                    materially_changed ||
                    player->second.title != title;
                player->second.title = title;
                g_variant_unref(title_value);
            }

            g_variant_unref(value);
        }
    }
    else
    {
        auto value = g_variant_lookup_value(
            properties,
            "DesktopEntry",
            G_VARIANT_TYPE_STRING);

        if (value)
        {
            const std::string desktop_entry =
                g_variant_get_string(value, nullptr);
            materially_changed =
                player->second.desktop_entry !=
                desktop_entry;
            player->second.desktop_entry =
                desktop_entry;
            g_variant_unref(value);
        }
    }

    g_variant_unref(properties);
    g_variant_unref(reply);
    g_clear_error(&error);

    if (materially_changed)
        state->changed.emit();
}

void request_properties(
    const std::shared_ptr<
        DockMediaPlaybackMonitor::State> &state,
    const std::string &service,
    bool player_properties)
{
    if (!state || !state->connection)
        return;

    const auto player =
        state->players.find(service);

    if (player == state->players.end())
        return;

    if (player_properties)
    {
        if (player->second.player_request_in_flight)
            return;

        player->second.player_request_in_flight = true;
    }

    auto request = new PropertyRequest{
        state,
        service,
        player_properties};

    g_dbus_connection_call(
        state->connection,
        service.c_str(),
        MPRIS_PATH,
        PROPERTIES_INTERFACE,
        "GetAll",
        g_variant_new(
            "(s)",
            player_properties
                ? MPRIS_PLAYER_INTERFACE
                : MPRIS_ROOT_INTERFACE),
        G_VARIANT_TYPE("(a{sv})"),
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        nullptr,
        properties_ready,
        request);
}

void properties_changed(
    GDBusConnection *,
    const gchar *,
    const gchar *,
    const gchar *,
    const gchar *,
    GVariant *,
    gpointer user_data)
{
    auto *request =
        static_cast<PropertyRequest *>(user_data);
    const auto state = request->state.lock();

    if (state && state->alive)
    {
        request_properties(
            state,
            request->service,
            true);
    }
}

void remove_player(
    const std::shared_ptr<
        DockMediaPlaybackMonitor::State> &state,
    const std::string &service)
{
    const auto player = state->players.find(service);

    if (player == state->players.end())
        return;

    if (player->second.properties_subscription != 0)
    {
        g_dbus_connection_signal_unsubscribe(
            state->connection,
            player->second.properties_subscription);
    }

    const bool was_playing = player->second.playing;
    state->players.erase(player);

    if (was_playing)
        state->changed.emit();
}

void add_player(
    const std::shared_ptr<
        DockMediaPlaybackMonitor::State> &state,
    const std::string &service)
{
    if (!state || !state->connection ||
        state->players.count(service) != 0)
    {
        return;
    }

    DockMediaPlaybackMonitor::State::Player player;
    auto request = new PropertyRequest{
        state,
        service,
        true};

    player.properties_subscription =
        g_dbus_connection_signal_subscribe(
            state->connection,
            service.c_str(),
            PROPERTIES_INTERFACE,
            "PropertiesChanged",
            MPRIS_PATH,
            MPRIS_PLAYER_INTERFACE,
            G_DBUS_SIGNAL_FLAGS_NONE,
            properties_changed,
            request,
            [](gpointer data)
            {
                delete static_cast<
                    PropertyRequest *>(data);
            });

    state->players.emplace(service, player);
    request_properties(state, service, false);
    request_properties(state, service, true);
}

void name_owner_changed(
    GDBusConnection *,
    const gchar *,
    const gchar *,
    const gchar *,
    const gchar *,
    GVariant *parameters,
    gpointer user_data)
{
    auto *weak_state = static_cast<
        std::weak_ptr<
            DockMediaPlaybackMonitor::State> *>(
        user_data);
    const auto state = weak_state->lock();

    if (!state || !state->alive)
        return;

    const char *name = nullptr;
    const char *old_owner = nullptr;
    const char *new_owner = nullptr;
    g_variant_get(
        parameters,
        "(&s&s&s)",
        &name,
        &old_owner,
        &new_owner);

    if (!is_mpris_name(name))
        return;

    if (new_owner && *new_owner)
    {
        // A service can replace its owner without first disappearing. Rebuild
        // the subscription and refresh both identity and playback state.
        if (old_owner && *old_owner)
            remove_player(state, name);

        add_player(state, name);
    }
    else
    {
        remove_player(state, name);
    }
}

}

DockMediaPlaybackMonitor::DockMediaPlaybackMonitor()
    : m_state(std::make_shared<State>())
{
    GError *error = nullptr;
    m_state->connection = g_bus_get_sync(
        G_BUS_TYPE_SESSION,
        nullptr,
        &error);

    if (!m_state->connection)
    {
        g_warning(
            "Cannot monitor MPRIS media playback: %s",
            error ? error->message : "unknown error");
        g_clear_error(&error);
        return;
    }

    auto weak_state = new std::weak_ptr<State>(m_state);
    m_state->names_subscription =
        g_dbus_connection_signal_subscribe(
            m_state->connection,
            DBUS_SERVICE,
            DBUS_INTERFACE,
            "NameOwnerChanged",
            DBUS_PATH,
            nullptr,
            G_DBUS_SIGNAL_FLAGS_NONE,
            name_owner_changed,
            weak_state,
            [](gpointer data)
            {
                delete static_cast<
                    std::weak_ptr<State> *>(data);
            });

    auto names_reply =
        g_dbus_connection_call_sync(
            m_state->connection,
            DBUS_SERVICE,
            DBUS_PATH,
            DBUS_INTERFACE,
            "ListNames",
            nullptr,
            G_VARIANT_TYPE("(as)"),
            G_DBUS_CALL_FLAGS_NONE,
            1000,
            nullptr,
            &error);

    if (names_reply)
    {
        GVariant *names = nullptr;
        g_variant_get(names_reply, "(@as)", &names);
        GVariantIter iterator;
        const char *name = nullptr;
        g_variant_iter_init(&iterator, names);

        while (g_variant_iter_next(
                   &iterator,
                   "&s",
                   &name))
        {
            if (is_mpris_name(name))
                add_player(m_state, name);
        }

        g_variant_unref(names);
        g_variant_unref(names_reply);
    }
    else
    {
        g_warning(
            "Cannot list MPRIS media players: %s",
            error ? error->message : "unknown error");
    }

    g_clear_error(&error);
}

DockMediaPlaybackMonitor::~DockMediaPlaybackMonitor()
{
    if (!m_state)
        return;

    m_state->alive = false;

    if (m_state->connection)
    {
        for (const auto &entry : m_state->players)
        {
            if (entry.second.properties_subscription != 0)
            {
                g_dbus_connection_signal_unsubscribe(
                    m_state->connection,
                    entry.second.properties_subscription);
            }
        }

        if (m_state->names_subscription != 0)
        {
            g_dbus_connection_signal_unsubscribe(
                m_state->connection,
                m_state->names_subscription);
        }

        g_object_unref(m_state->connection);
        m_state->connection = nullptr;
    }
}

bool DockMediaPlaybackMonitor::is_playing(
    const std::string &desktop_id) const
{
    if (!m_state)
        return false;

    for (const auto &entry : m_state->players)
    {
        const std::string service_application =
            entry.first.substr(
                sizeof(MPRIS_PREFIX) - 1);

        if (identifiers_match(
                desktop_id,
                entry.second.desktop_entry) ||
            identifiers_match(
                desktop_id,
                service_application))
        {
            return true;
        }
    }

    return false;
}

bool DockMediaPlaybackMonitor::should_stream(
    const std::string &desktop_id) const
{
    if (!m_state)
        return false;

    for (const auto &entry : m_state->players)
    {
        const std::string service_application =
            entry.first.substr(
                sizeof(MPRIS_PREFIX) - 1);
        const bool matches =
            identifiers_match(
                desktop_id,
                entry.second.desktop_entry) ||
            identifiers_match(
                desktop_id,
                service_application);

        if (!matches)
            continue;

        if (entry.second.playing)
            return true;

        // Firefox can retain valid media metadata while incorrectly exposing
        // PlaybackStatus=Paused for an actively playing browser tab. In that
        // case stream the active visible Firefox window; KWin only produces
        // new buffers when its contents are damaged.
        if (!entry.second.title.empty() &&
            browser_family(desktop_id) == "firefox")
        {
            return true;
        }
    }

    return false;
}

std::string DockMediaPlaybackMonitor::playing_title(
    const std::string &desktop_id) const
{
    if (!m_state)
        return {};

    for (const auto &entry : m_state->players)
    {
        if (!entry.second.playing)
            continue;

        const std::string service_application =
            entry.first.substr(
                sizeof(MPRIS_PREFIX) - 1);

        if (identifiers_match(
                desktop_id,
                entry.second.desktop_entry) ||
            identifiers_match(
                desktop_id,
                service_application))
        {
            if (entry.second.playing ||
                (!entry.second.title.empty() &&
                 browser_family(desktop_id) ==
                     "firefox"))
            {
                return entry.second.title;
            }
        }
    }

    return {};
}

sigc::signal<void> &
DockMediaPlaybackMonitor::signal_changed()
{
    return m_state->changed;
}
