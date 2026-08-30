// ------------------------------------------------------------
// Docklight 6.0
//
// Uses Hyprland's JSON request API for snapshots and its event socket to
// invalidate them. Window identities prefer stableId and fall back to the
// compositor address on older Hyprland releases.
// ------------------------------------------------------------

#include "hyprland_window_backend.h"

#include <glib-unix.h>
#include <glib.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <set>
#include <utility>

namespace
{

std::size_t skip_space(
    const std::string &json,
    std::size_t position)
{
    while (position < json.size() &&
           std::isspace(
               static_cast<unsigned char>(json[position])))
    {
        ++position;
    }
    return position;
}

std::optional<std::size_t> value_position(
    const std::string &json,
    const std::string &key)
{
    const auto found = json.find(
        "\"" + key + "\"");
    if (found == std::string::npos)
        return std::nullopt;

    const auto colon = json.find(':', found + key.size() + 2);
    if (colon == std::string::npos)
        return std::nullopt;

    return skip_space(json, colon + 1);
}

void append_codepoint(
    std::string &output,
    unsigned int codepoint)
{
    char encoded[7] = {};
    const auto length = g_unichar_to_utf8(
        static_cast<gunichar>(codepoint),
        encoded);
    output.append(encoded, static_cast<std::size_t>(length));
}

std::optional<std::string> json_string(
    const std::string &json,
    const std::string &key)
{
    const auto position = value_position(json, key);
    if (!position || *position >= json.size() ||
        json[*position] != '"')
    {
        return std::nullopt;
    }

    std::string result;
    for (auto cursor = *position + 1;
         cursor < json.size();
         ++cursor)
    {
        const char character = json[cursor];
        if (character == '"')
            return result;
        if (character != '\\')
        {
            result.push_back(character);
            continue;
        }

        if (++cursor >= json.size())
            return std::nullopt;
        switch (json[cursor])
        {
        case '"': result.push_back('"'); break;
        case '\\': result.push_back('\\'); break;
        case '/': result.push_back('/'); break;
        case 'b': result.push_back('\b'); break;
        case 'f': result.push_back('\f'); break;
        case 'n': result.push_back('\n'); break;
        case 'r': result.push_back('\r'); break;
        case 't': result.push_back('\t'); break;
        case 'u':
        {
            if (cursor + 4 >= json.size())
                return std::nullopt;
            unsigned int codepoint = 0;
            for (int digit = 0; digit < 4; ++digit)
            {
                const auto value = g_ascii_xdigit_value(
                    json[++cursor]);
                if (value < 0)
                    return std::nullopt;
                codepoint = codepoint * 16U +
                            static_cast<unsigned int>(value);
            }
            append_codepoint(result, codepoint);
            break;
        }
        default:
            return std::nullopt;
        }
    }

    return std::nullopt;
}

std::optional<long long> json_integer(
    const std::string &json,
    const std::string &key)
{
    const auto position = value_position(json, key);
    if (!position)
        return std::nullopt;

    char *end = nullptr;
    errno = 0;
    const auto value = std::strtoll(
        json.c_str() + *position,
        &end,
        10);
    if (errno != 0 || end == json.c_str() + *position)
        return std::nullopt;
    return value;
}

std::optional<bool> json_boolean(
    const std::string &json,
    const std::string &key)
{
    const auto position = value_position(json, key);
    if (!position)
        return std::nullopt;
    if (json.compare(*position, 4, "true") == 0)
        return true;
    if (json.compare(*position, 5, "false") == 0)
        return false;
    return std::nullopt;
}

std::optional<std::string> json_compound(
    const std::string &json,
    const std::string &key,
    char opening,
    char closing)
{
    const auto position = value_position(json, key);
    if (!position || *position >= json.size() ||
        json[*position] != opening)
    {
        return std::nullopt;
    }

    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (auto cursor = *position;
         cursor < json.size();
         ++cursor)
    {
        const char character = json[cursor];
        if (quoted)
        {
            if (escaped)
                escaped = false;
            else if (character == '\\')
                escaped = true;
            else if (character == '"')
                quoted = false;
            continue;
        }
        if (character == '"')
            quoted = true;
        else if (character == opening)
            ++depth;
        else if (character == closing && --depth == 0)
        {
            return json.substr(
                *position,
                cursor - *position + 1);
        }
    }
    return std::nullopt;
}

std::vector<std::string> top_level_objects(
    const std::string &json)
{
    std::vector<std::string> objects;
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    std::size_t start = 0;

    for (std::size_t cursor = 0;
         cursor < json.size();
         ++cursor)
    {
        const char character = json[cursor];
        if (quoted)
        {
            if (escaped)
                escaped = false;
            else if (character == '\\')
                escaped = true;
            else if (character == '"')
                quoted = false;
            continue;
        }
        if (character == '"')
            quoted = true;
        else if (character == '{')
        {
            if (depth++ == 0)
                start = cursor;
        }
        else if (character == '}' && depth > 0 && --depth == 0)
        {
            objects.push_back(
                json.substr(start, cursor - start + 1));
        }
    }
    return objects;
}

std::pair<int, int> json_integer_pair(
    const std::string &json,
    const std::string &key)
{
    const auto array = json_compound(json, key, '[', ']');
    if (!array)
        return {};

    const auto *first_start =
        array->c_str() + 1;
    char *end = nullptr;
    errno = 0;
    const auto first = std::strtoll(
        first_start,
        &end,
        10);
    if (errno != 0 || end == first_start)
        return {};
    const auto comma = array->find(
        ',',
        static_cast<std::size_t>(end - array->c_str()));
    if (comma == std::string::npos)
        return {};
    const auto *second_start =
        array->c_str() + comma + 1;
    errno = 0;
    const auto second = std::strtoll(
        second_start,
        &end,
        10);
    if (errno != 0 || end == second_start ||
        first < std::numeric_limits<int>::min() ||
        first > std::numeric_limits<int>::max() ||
        second < std::numeric_limits<int>::min() ||
        second > std::numeric_limits<int>::max())
    {
        return {};
    }
    return {
        static_cast<int>(first),
        static_cast<int>(second)};
}

std::optional<std::string> run_hyprctl_capture(
    const std::vector<std::string> &arguments)
{
    std::vector<std::string> storage{"hyprctl"};
    storage.insert(
        storage.end(),
        arguments.begin(),
        arguments.end());
    std::vector<char *> argv;
    for (auto &argument : storage)
        argv.push_back(argument.data());
    argv.push_back(nullptr);

    char *standard_output = nullptr;
    char *standard_error = nullptr;
    int status = 0;
    GError *error = nullptr;
    const bool spawned = g_spawn_sync(
        nullptr,
        argv.data(),
        nullptr,
        G_SPAWN_SEARCH_PATH,
        nullptr,
        nullptr,
        &standard_output,
        &standard_error,
        &status,
        &error);
    const bool succeeded =
        spawned &&
        g_spawn_check_wait_status(status, nullptr);
    std::optional<std::string> result;
    if (succeeded && standard_output)
        result = standard_output;
    else if (error)
        g_warning("Cannot query Hyprland: %s", error->message);

    g_clear_error(&error);
    g_free(standard_output);
    g_free(standard_error);
    return result;
}

bool run_hyprctl_command(
    const std::vector<std::string> &arguments)
{
    return run_hyprctl_capture(arguments).has_value();
}

bool version_uses_lua_dispatch(
    const std::string &version)
{
    const auto marker = version.find("Hyprland 0.");
    if (marker == std::string::npos)
        return false;
    const auto minor_start = marker + std::strlen("Hyprland 0.");
    char *end = nullptr;
    const auto minor = std::strtol(
        version.c_str() + minor_start,
        &end,
        10);
    // Hyprland 0.55.2 still uses the legacy dispatcher command even though
    // newer development documentation describes the Lua dispatcher API.
    return end != version.c_str() + minor_start && minor > 55;
}

} // namespace

HyprlandSnapshot parse_hyprland_snapshot(
    const std::string &clients_json,
    const std::string &monitors_json,
    const std::string &active_window_json)
{
    HyprlandSnapshot snapshot;
    std::set<long long> active_workspaces;

    for (const auto &monitor : top_level_objects(monitors_json))
    {
        const auto workspace = json_compound(
            monitor,
            "activeWorkspace",
            '{',
            '}');
        if (workspace)
        {
            const auto id = json_integer(*workspace, "id");
            if (id)
                active_workspaces.insert(*id);
        }
    }

    const auto active_stable_id = json_string(
        active_window_json,
        "stableId");
    const auto active_address = json_string(
        active_window_json,
        "address");

    struct OrderedWindow
    {
        ManagedWindow window;
        long long focus_history =
            std::numeric_limits<long long>::max();
    };
    std::vector<OrderedWindow> ordered;

    for (const auto &client : top_level_objects(clients_json))
    {
        if (!json_boolean(client, "mapped").value_or(true))
            continue;

        const auto address = json_string(client, "address");
        const auto stable_id = json_string(client, "stableId");
        const auto id = stable_id && !stable_id->empty()
                            ? *stable_id
                            : address.value_or("");
        if (id.empty())
            continue;

        ManagedWindow window;
        window.id = id;
        window.desktop_file_name =
            json_string(client, "class").value_or("");
        if (window.desktop_file_name.empty())
        {
            window.desktop_file_name =
                json_string(client, "initialClass").value_or("");
        }
        window.icon_name = window.desktop_file_name;
        window.caption =
            json_string(client, "title").value_or("");
        window.process_id =
            json_integer(client, "pid").value_or(0);

        const auto position = json_integer_pair(client, "at");
        const auto size = json_integer_pair(client, "size");
        window.frame_geometry = {
            position.first,
            position.second,
            size.first,
            size.second};
        window.maximized =
            json_integer(client, "fullscreen").value_or(0) == 1;
        window.minimized = false;

        const auto workspace = json_compound(
            client,
            "workspace",
            '{',
            '}');
        if (workspace)
        {
            const auto workspace_id = json_integer(*workspace, "id");
            const auto workspace_name = json_string(*workspace, "name");
            if (workspace_name && !workspace_name->empty())
                window.desktop_ids.push_back(*workspace_name);
            if (workspace_id && *workspace_id > 0 &&
                *workspace_id <=
                    std::numeric_limits<unsigned int>::max())
            {
                window.desktop_numbers.push_back(
                    static_cast<unsigned int>(*workspace_id));
            }
            window.on_current_desktop =
                workspace_id &&
                active_workspaces.count(*workspace_id) != 0;
        }

        window.active =
            (active_stable_id && *active_stable_id == id) ||
            (active_address && address &&
             *active_address == *address);
        if (window.active)
            snapshot.active_window = id;
        if (address)
            snapshot.addresses[id] = *address;

        ordered.push_back({
            std::move(window),
            json_integer(client, "focusHistoryID")
                .value_or(std::numeric_limits<long long>::max())});
    }

    std::stable_sort(
        ordered.begin(),
        ordered.end(),
        [](const OrderedWindow &left, const OrderedWindow &right)
        {
            return left.focus_history > right.focus_history;
        });
    for (auto &entry : ordered)
    {
        snapshot.stacking_order.push_back(entry.window.id);
        snapshot.windows.push_back(std::move(entry.window));
    }
    return snapshot;
}

HyprlandWindowBackend::HyprlandWindowBackend()
    : m_query_handler(run_hyprctl_capture),
      m_command_handler(run_hyprctl_command),
      m_manage_event_socket(true)
{
}

HyprlandWindowBackend::HyprlandWindowBackend(
    QueryHandler query_handler,
    CommandHandler command_handler)
    : m_query_handler(std::move(query_handler)),
      m_command_handler(std::move(command_handler))
{
}

HyprlandWindowBackend::~HyprlandWindowBackend()
{
    stop();
}

void HyprlandWindowBackend::start()
{
    if (m_started)
        return;
    m_started = true;
    if (const auto version = m_query_handler({"version"}))
        m_lua_dispatch = version_uses_lua_dispatch(*version);
    refresh();
    if (m_manage_event_socket)
        connect_event_socket();
}

void HyprlandWindowBackend::stop()
{
    if (!m_started)
        return;
    disconnect_event_socket();
    if (m_refresh_source != 0)
    {
        g_source_remove(m_refresh_source);
        m_refresh_source = 0;
    }
    const bool was_connected = m_connected;
    m_connected = false;
    m_started = false;
    m_snapshot = {};
    if (was_connected)
        notify_connection_changed(false);
}

std::string HyprlandWindowBackend::name() const
{
    return "Hyprland";
}

WindowBackendCapabilities HyprlandWindowBackend::capabilities() const
{
    WindowBackendCapabilities result;
    result.can_activate = true;
    result.can_raise = true;
    result.can_close = true;
    result.can_maximize = true;
    result.provides_stacking_order = true;
    result.provides_virtual_desktops = true;
    result.provides_frame_geometry = true;
    result.thumbnail_policy =
        WindowThumbnailPolicy::capture_on_demand;
    return result;
}

bool HyprlandWindowBackend::connected() const { return m_connected; }
std::vector<ManagedWindow> HyprlandWindowBackend::windows() const { return m_snapshot.windows; }
std::vector<WindowId> HyprlandWindowBackend::stacking_order() const { return m_snapshot.stacking_order; }
std::optional<WindowId> HyprlandWindowBackend::active_window() const { return m_snapshot.active_window; }
std::optional<WindowIconGeometry> HyprlandWindowBackend::dock_surface_geometry() const { return std::nullopt; }

std::string HyprlandWindowBackend::selector(const WindowId &window_id) const
{
    if (window_id.rfind("0x", 0) == 0)
        return "address:" + window_id;
    return "stableid:" + window_id;
}

bool HyprlandWindowBackend::dispatch_lua(const std::string &expression)
{
    const bool accepted = m_connected &&
        m_command_handler({"dispatch", expression});
    if (accepted)
        schedule_refresh();
    return accepted;
}

bool HyprlandWindowBackend::dispatch_legacy(
    const std::string &dispatcher,
    const std::string &arguments)
{
    const bool accepted = m_connected &&
        m_command_handler({"dispatch", dispatcher, arguments});
    if (accepted)
        schedule_refresh();
    return accepted;
}

bool HyprlandWindowBackend::activate_window(const WindowId &window_id)
{
    if (m_lua_dispatch)
    {
        return dispatch_lua(
            "hl.dsp.focus({ window = \"" +
            selector(window_id) + "\" })");
    }
    const auto address = m_snapshot.addresses.find(window_id);
    return address != m_snapshot.addresses.end() &&
           dispatch_legacy("focuswindow", "address:" + address->second);
}

bool HyprlandWindowBackend::present_windows(
    const std::vector<WindowId> &window_ids)
{
    return !window_ids.empty() &&
           activate_window(window_ids.back());
}

bool HyprlandWindowBackend::hide_windows(const std::vector<WindowId> &) { return false; }
bool HyprlandWindowBackend::raise_window(const WindowId &window_id) { return activate_window(window_id); }

bool HyprlandWindowBackend::close_window(const WindowId &window_id)
{
    if (m_lua_dispatch)
    {
        return dispatch_lua(
            "hl.dsp.window.close({ window = \"" +
            selector(window_id) + "\" })");
    }
    const auto address = m_snapshot.addresses.find(window_id);
    return address != m_snapshot.addresses.end() &&
           dispatch_legacy("closewindow", "address:" + address->second);
}

bool HyprlandWindowBackend::set_window_minimized(const WindowId &, bool) { return false; }

bool HyprlandWindowBackend::set_window_maximized(
    const WindowId &window_id,
    bool maximized)
{
    if (!m_lua_dispatch)
    {
        return activate_window(window_id) &&
               dispatch_legacy(
                   "fullscreen",
                   std::string("1 ") +
                       (maximized ? "set" : "unset"));
    }
    return dispatch_lua(
        "hl.dsp.window.fullscreen({ window = \"" +
        selector(window_id) +
        "\", mode = \"maximized\", action = \"" +
        (maximized ? "enable" : "disable") + "\" })");
}

bool HyprlandWindowBackend::set_window_icon_geometry(
    const WindowId &,
    const WindowIconGeometry &)
{
    return false;
}

void HyprlandWindowBackend::refresh()
{
    if (!m_started)
        return;
    const auto clients = m_query_handler({"-j", "clients"});
    const auto monitors = m_query_handler({"-j", "monitors"});
    const auto active = m_query_handler({"-j", "activewindow"});
    if (!clients || !monitors || !active)
    {
        if (m_connected)
        {
            m_connected = false;
            notify_connection_changed(false);
        }
        return;
    }

    m_snapshot = parse_hyprland_snapshot(
        *clients,
        *monitors,
        *active);
    if (!m_connected)
    {
        m_connected = true;
        notify_connection_changed(true);
    }
    else
    {
        notify_snapshot_changed();
    }
}

void HyprlandWindowBackend::schedule_refresh()
{
    if (!m_started || m_refresh_source != 0)
        return;
    m_refresh_source = g_timeout_add(
        35,
        on_refresh_timeout,
        this);
}

gboolean HyprlandWindowBackend::on_refresh_timeout(gpointer data)
{
    auto *self = static_cast<HyprlandWindowBackend *>(data);
    self->m_refresh_source = 0;
    self->refresh();
    return G_SOURCE_REMOVE;
}

void HyprlandWindowBackend::connect_event_socket()
{
    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    const char *signature = std::getenv("HYPRLAND_INSTANCE_SIGNATURE");
    if (!runtime || !signature)
        return;
    const std::string path = std::string(runtime) +
        "/hypr/" + signature + "/.socket2.sock";
    m_event_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (m_event_fd < 0)
        return;
    sockaddr_un address{};
    if (path.size() >= sizeof(address.sun_path))
    {
        close(m_event_fd);
        m_event_fd = -1;
        return;
    }
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    if (connect(
            m_event_fd,
            reinterpret_cast<sockaddr *>(&address),
            sizeof(address)) != 0)
    {
        close(m_event_fd);
        m_event_fd = -1;
        return;
    }
    m_event_source = g_unix_fd_add(
        m_event_fd,
        static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR),
        on_event_socket,
        this);
}

void HyprlandWindowBackend::disconnect_event_socket()
{
    if (m_event_source != 0)
    {
        g_source_remove(m_event_source);
        m_event_source = 0;
    }
    if (m_event_fd >= 0)
    {
        close(m_event_fd);
        m_event_fd = -1;
    }
}

gboolean HyprlandWindowBackend::on_event_socket(
    gint fd,
    GIOCondition condition,
    gpointer data)
{
    auto *self = static_cast<HyprlandWindowBackend *>(data);
    if ((condition &
         (G_IO_HUP | G_IO_ERR | G_IO_NVAL)) != 0)
    {
        self->m_event_source = 0;
        close(self->m_event_fd);
        self->m_event_fd = -1;
        return G_SOURCE_REMOVE;
    }
    char buffer[4096];
    const auto count = read(fd, buffer, sizeof(buffer));
    if (count <= 0)
    {
        self->m_event_source = 0;
        close(self->m_event_fd);
        self->m_event_fd = -1;
        return G_SOURCE_REMOVE;
    }

    self->schedule_refresh();
    return G_SOURCE_CONTINUE;
}
