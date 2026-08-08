// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window_thumbnail_provider.cpp
//
// Implementation overview:
// Implements asynchronous static thumbnail capture through KWin's
// ScreenShot2 interface.
//
// Important implementation decisions:
// - D-Bus requests do not block the GTK main loop.
// - Returned file descriptors and pixel buffers are validated before
//   conversion.
// - Completion checks shared lifetime state before invoking callbacks.
//
// ------------------------------------------------------------

#include "dock_window_thumbnail_provider.h"

#include <gio/gio.h>
#include <glib.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <thread>
#include <utility>
#include <vector>

#include <unistd.h>

namespace
{

constexpr char SCREENSHOT_SERVICE[] =
    "org.kde.KWin.ScreenShot2";
constexpr char SCREENSHOT_PATH[] =
    "/org/kde/KWin/ScreenShot2";
constexpr char SCREENSHOT_INTERFACE[] =
    "org.kde.KWin.ScreenShot2";

struct Completion
{
    std::weak_ptr<DockWindowThumbnailProvider::State> state;
    WindowId window_id;
    int source_width = 0;
    int source_height = 0;
    int target_width = 0;
    int target_height = 0;
    std::vector<unsigned char> rgba;
    DockWindowThumbnailProvider::Callback callback;
};

int variant_integer(
    GVariant *values,
    const char *key)
{
    auto value =
        g_variant_lookup_value(values, key, nullptr);

    if (!value)
        return 0;

    int result = 0;

    if (g_variant_is_of_type(
            value,
            G_VARIANT_TYPE_INT32))
    {
        result = g_variant_get_int32(value);
    }
    else if (g_variant_is_of_type(
                 value,
                 G_VARIANT_TYPE_UINT32))
    {
        result = static_cast<int>(
            g_variant_get_uint32(value));
    }

    g_variant_unref(value);
    return result;
}

gboolean deliver_thumbnail(gpointer data)
{
    std::unique_ptr<Completion> completion(
        static_cast<Completion *>(data));

    const auto state = completion->state.lock();

    if (!state || !state->alive)
        return G_SOURCE_REMOVE;

    Glib::RefPtr<Gdk::Pixbuf> thumbnail;

    if (!completion->rgba.empty() &&
        completion->source_width > 0 &&
        completion->source_height > 0)
    {
        auto source = Gdk::Pixbuf::create(
            Gdk::COLORSPACE_RGB,
            true,
            8,
            completion->source_width,
            completion->source_height);

        if (source)
        {
            const int destination_stride =
                source->get_rowstride();

            for (int y = 0;
                 y < completion->source_height;
                 ++y)
            {
                std::copy_n(
                    completion->rgba.data() +
                        y * completion->source_width * 4,
                    completion->source_width * 4,
                    source->get_pixels() +
                        y * destination_stride);
            }

            const double scale = std::min(
                static_cast<double>(
                    completion->target_width) /
                    completion->source_width,
                static_cast<double>(
                    completion->target_height) /
                    completion->source_height);
            const int scaled_width = std::max(
                1,
                static_cast<int>(std::lround(
                    completion->source_width * scale)));
            const int scaled_height = std::max(
                1,
                static_cast<int>(std::lround(
                    completion->source_height * scale)));

            thumbnail = source->scale_simple(
                scaled_width,
                scaled_height,
                Gdk::INTERP_BILINEAR);
        }
    }

    completion->callback(
        completion->window_id,
        thumbnail);

    return G_SOURCE_REMOVE;
}

void schedule_delivery(
    std::unique_ptr<Completion> completion)
{
    g_idle_add_full(
        G_PRIORITY_DEFAULT_IDLE,
        deliver_thumbnail,
        completion.release(),
        nullptr);
}

void capture(
    std::weak_ptr<DockWindowThumbnailProvider::State> state,
    WindowId window_id,
    int target_width,
    int target_height,
    DockWindowThumbnailProvider::Callback callback)
{
    auto completion =
        std::make_unique<Completion>();
    completion->state = std::move(state);
    completion->window_id = std::move(window_id);
    completion->target_width = target_width;
    completion->target_height = target_height;
    completion->callback = std::move(callback);

    // Hold the provider state for the entire capture so its shared session
    // connection cannot be released while this worker is using it.
    const auto active_state =
        completion->state.lock();

    if (!active_state ||
        !active_state->alive ||
        !active_state->connection)
    {
        schedule_delivery(std::move(completion));
        return;
    }

    int pipe_fds[2] = {-1, -1};

    if (pipe(pipe_fds) != 0)
    {
        schedule_delivery(std::move(completion));
        return;
    }

    std::vector<unsigned char> raw;

    std::thread reader(
        [&raw, read_fd = pipe_fds[0]]()
        {
            unsigned char buffer[65536];

            while (true)
            {
                const auto count = read(
                    read_fd,
                    buffer,
                    sizeof(buffer));

                if (count > 0)
                {
                    raw.insert(
                        raw.end(),
                        buffer,
                        buffer + count);
                }
                else if (count == 0 ||
                         errno != EINTR)
                {
                    break;
                }
            }

            close(read_fd);
        });

    GError *error = nullptr;
    auto connection = active_state->connection;

    GVariant *reply = nullptr;

    if (connection)
    {
        auto fd_list = g_unix_fd_list_new();
        const int handle =
            g_unix_fd_list_append(
                fd_list,
                pipe_fds[1],
                &error);

        if (handle >= 0)
        {
            GVariantBuilder options;
            g_variant_builder_init(
                &options,
                G_VARIANT_TYPE("a{sv}"));
            g_variant_builder_add(
                &options,
                "{sv}",
                "include-cursor",
                g_variant_new_boolean(false));
            g_variant_builder_add(
                &options,
                "{sv}",
                "native-resolution",
                g_variant_new_boolean(true));

            reply =
                g_dbus_connection_call_with_unix_fd_list_sync(
                    connection,
                    SCREENSHOT_SERVICE,
                    SCREENSHOT_PATH,
                    SCREENSHOT_INTERFACE,
                    "CaptureWindow",
                    g_variant_new(
                        "(s@a{sv}h)",
                        completion->window_id.c_str(),
                        g_variant_builder_end(&options),
                        handle),
                    G_VARIANT_TYPE("(a{sv})"),
                    G_DBUS_CALL_FLAGS_NONE,
                    5000,
                    fd_list,
                    nullptr,
                    nullptr,
                    &error);
        }

        g_object_unref(fd_list);
    }

    close(pipe_fds[1]);
    reader.join();

    if (!reply)
    {
        if (error)
        {
            g_warning(
                "Cannot capture window thumbnail: %s",
                error->message);
        }

        g_clear_error(&error);
        schedule_delivery(std::move(completion));
        return;
    }

    GVariant *results = nullptr;
    g_variant_get(reply, "(@a{sv})", &results);

    const int width =
        variant_integer(results, "width");
    const int height =
        variant_integer(results, "height");
    int stride =
        variant_integer(results, "stride");

    if (stride <= 0)
        stride = width * 4;

    g_variant_unref(results);
    g_variant_unref(reply);

    if (width <= 0 || height <= 0 ||
        stride < width * 4 ||
        raw.size() <
            static_cast<std::size_t>(stride) *
                height)
    {
        schedule_delivery(std::move(completion));
        return;
    }

    completion->source_width = width;
    completion->source_height = height;
    completion->rgba.resize(
        static_cast<std::size_t>(width) *
        height * 4);

    // ScreenShot2 currently exports QImage ARGB32 premultiplied bytes. On
    // little-endian systems that is BGRA; convert and unpremultiply for
    // GdkPixbuf's straight-alpha RGBA representation.
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto source_index =
                static_cast<std::size_t>(y) *
                    stride +
                x * 4;
            const auto target_index =
                (static_cast<std::size_t>(y) *
                     width +
                 x) *
                4;

            const unsigned int alpha =
                raw[source_index + 3];

            auto unpremultiply =
                [alpha](unsigned int value)
                {
                    return static_cast<unsigned char>(
                        alpha == 0
                            ? 0
                            : std::min(
                                  255U,
                                  (value * 255U +
                                   alpha / 2U) /
                                      alpha));
                };

            completion->rgba[target_index] =
                unpremultiply(raw[source_index + 2]);
            completion->rgba[target_index + 1] =
                unpremultiply(raw[source_index + 1]);
            completion->rgba[target_index + 2] =
                unpremultiply(raw[source_index]);
            completion->rgba[target_index + 3] =
                static_cast<unsigned char>(alpha);
        }
    }

    bool has_visible_pixel = false;

    for (std::size_t alpha_index = 3;
         alpha_index < completion->rgba.size();
         alpha_index += 4)
    {
        if (completion->rgba[alpha_index] != 0)
        {
            has_visible_pixel = true;
            break;
        }
    }

    if (!has_visible_pixel)
        completion->rgba.clear();

    schedule_delivery(std::move(completion));
}

}

DockWindowThumbnailProvider::
    DockWindowThumbnailProvider()
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
            "Cannot connect thumbnail provider to the session bus: %s",
            error ? error->message : "unknown error");
    }

    g_clear_error(&error);
}

DockWindowThumbnailProvider::
    ~DockWindowThumbnailProvider()
{
    m_state->alive = false;
}

void DockWindowThumbnailProvider::request(
    const WindowId &window_id,
    int target_width,
    int target_height,
    Callback callback)
{
    if (window_id.empty() ||
        target_width <= 0 ||
        target_height <= 0 ||
        !callback)
    {
        return;
    }

    std::thread(
        capture,
        std::weak_ptr<State>(m_state),
        window_id,
        target_width,
        target_height,
        std::move(callback))
        .detach();
}
