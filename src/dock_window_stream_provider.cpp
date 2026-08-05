// KWin private screencast protocol and PipeWire window-stream consumer.

#include "dock_window_stream_provider.h"

#include "zkde-screencast-unstable-v1-client-protocol.h"

#include <gdk/gdkwayland.h>
#include <glib.h>
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wvariadic-macros"
#endif
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/raw-utils.h>
#include <wayland-client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace
{

struct StreamState;

struct FrameDelivery
{
    std::weak_ptr<StreamState> stream;
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

struct StreamState
{
    WindowId window_id;
    int target_width = 0;
    int target_height = 0;
    DockWindowStreamProvider::Callback callback;
    zkde_screencast_stream_unstable_v1 *wayland_stream = nullptr;
    pw_thread_loop *loop = nullptr;
    pw_stream *pipewire_stream = nullptr;
    spa_video_info_raw video_info{};
    std::atomic<bool> alive{true};
    std::atomic<bool> delivery_pending{false};
    std::atomic<bool> first_frame_delivered{false};
};

gboolean deliver_frame(gpointer data)
{
    std::unique_ptr<FrameDelivery> delivery(
        static_cast<FrameDelivery *>(data));
    const auto stream = delivery->stream.lock();

    if (!stream || !stream->alive)
        return G_SOURCE_REMOVE;

    auto pixbuf = Gdk::Pixbuf::create(
        Gdk::COLORSPACE_RGB,
        true,
        8,
        delivery->width,
        delivery->height);

    if (pixbuf)
    {
        const int row_size = delivery->width * 4;

        for (int y = 0; y < delivery->height; ++y)
        {
            std::copy_n(
                delivery->rgba.data() + y * row_size,
                row_size,
                pixbuf->get_pixels() + y * pixbuf->get_rowstride());
        }

        stream->callback(stream->window_id, pixbuf);
    }

    stream->delivery_pending = false;
    return G_SOURCE_REMOVE;
}

bool copy_scaled_frame(
    StreamState &stream,
    const spa_data &data,
    int &scaled_width,
    int &scaled_height,
    std::vector<unsigned char> &rgba)
{
    const auto width = static_cast<int>(stream.video_info.size.width);
    const auto height = static_cast<int>(stream.video_info.size.height);

    if (!data.data || !data.chunk || width <= 0 || height <= 0 ||
        stream.target_width <= 0 || stream.target_height <= 0)
    {
        return false;
    }

    const int stride = data.chunk->stride != 0
                           ? std::abs(data.chunk->stride)
                           : width * 4;

    if (stride < width * 4 ||
        data.chunk->offset > data.maxsize ||
        static_cast<std::uint64_t>(data.chunk->offset) +
                static_cast<std::uint64_t>(stride) * height >
            data.maxsize)
    {
        return false;
    }

    const auto *source = static_cast<const unsigned char *>(data.data) +
                         data.chunk->offset;
    const auto format = stream.video_info.format;

    if (format != SPA_VIDEO_FORMAT_BGRA &&
        format != SPA_VIDEO_FORMAT_BGRx &&
        format != SPA_VIDEO_FORMAT_RGBA &&
        format != SPA_VIDEO_FORMAT_RGBx)
    {
        return false;
    }

    const double scale = std::min(
        static_cast<double>(stream.target_width) / width,
        static_cast<double>(stream.target_height) / height);
    scaled_width = std::max(
        1,
        static_cast<int>(std::lround(width * scale)));
    scaled_height = std::max(
        1,
        static_cast<int>(std::lround(height * scale)));

    rgba.resize(
        static_cast<std::size_t>(scaled_width) *
        scaled_height * 4);

    const bool blue_first =
        format == SPA_VIDEO_FORMAT_BGRA ||
        format == SPA_VIDEO_FORMAT_BGRx;
    const bool has_alpha =
        format == SPA_VIDEO_FORMAT_BGRA ||
        format == SPA_VIDEO_FORMAT_RGBA;

    auto channel =
        [blue_first](const unsigned char *pixel, int component)
        {
            if (!blue_first)
                return static_cast<double>(pixel[component]);

            if (component == 0)
                return static_cast<double>(pixel[2]);
            if (component == 2)
                return static_cast<double>(pixel[0]);
            return static_cast<double>(pixel[component]);
        };

    for (int y = 0; y < scaled_height; ++y)
    {
        const double source_y = std::max(
            0.0,
            (y + 0.5) * height / scaled_height - 0.5);
        const int y0 = std::min(
            height - 1,
            static_cast<int>(std::floor(source_y)));
        const int y1 = std::min(height - 1, y0 + 1);
        const double y_fraction = source_y - y0;
        const auto *row0 = source + y0 * stride;
        const auto *row1 = source + y1 * stride;
        auto *destination_row = rgba.data() +
                                static_cast<std::size_t>(y) *
                                    scaled_width * 4;

        for (int x = 0; x < scaled_width; ++x)
        {
            const double source_x = std::max(
                0.0,
                (x + 0.5) * width / scaled_width - 0.5);
            const int x0 = std::min(
                width - 1,
                static_cast<int>(std::floor(source_x)));
            const int x1 = std::min(width - 1, x0 + 1);
            const double x_fraction = source_x - x0;
            const auto *top_left = row0 + x0 * 4;
            const auto *top_right = row0 + x1 * 4;
            const auto *bottom_left = row1 + x0 * 4;
            const auto *bottom_right = row1 + x1 * 4;
            auto *destination = destination_row + x * 4;

            for (int component = 0; component < 3; ++component)
            {
                const double top =
                    channel(top_left, component) *
                        (1.0 - x_fraction) +
                    channel(top_right, component) *
                        x_fraction;
                const double bottom =
                    channel(bottom_left, component) *
                        (1.0 - x_fraction) +
                    channel(bottom_right, component) *
                        x_fraction;
                destination[component] =
                    static_cast<unsigned char>(std::clamp(
                        std::lround(
                            top * (1.0 - y_fraction) +
                            bottom * y_fraction),
                        0L,
                        255L));
            }

            const double top_alpha =
                top_left[3] * (1.0 - x_fraction) +
                top_right[3] * x_fraction;
            const double bottom_alpha =
                bottom_left[3] * (1.0 - x_fraction) +
                bottom_right[3] * x_fraction;
            destination[3] = has_alpha
                                 ? static_cast<unsigned char>(
                                       std::clamp(
                                           std::lround(
                                               top_alpha *
                                                   (1.0 - y_fraction) +
                                               bottom_alpha *
                                                   y_fraction),
                                           0L,
                                           255L))
                                 : 255;
        }
    }

    return true;
}

void on_stream_param_changed(
    void *data,
    uint32_t id,
    const spa_pod *param)
{
    auto &stream = *static_cast<StreamState *>(data);

    if (id != SPA_PARAM_Format || !param)
        return;

    spa_format_video_raw_parse(param, &stream.video_info);
}

struct StreamCallbackData
{
    std::weak_ptr<StreamState> state;
};

struct WaylandCallbackData
{
    void *provider = nullptr;
    std::weak_ptr<StreamState> state;
};

void on_process_with_owner(void *data)
{
    auto &owner = *static_cast<StreamCallbackData *>(data);
    const auto stream = owner.state.lock();

    if (!stream || !stream->alive)
        return;

    auto *buffer = pw_stream_dequeue_buffer(stream->pipewire_stream);

    if (!buffer)
        return;

    auto *spa_buffer = buffer->buffer;

    if (spa_buffer && spa_buffer->n_datas > 0 &&
        !stream->delivery_pending.exchange(true))
    {
        auto delivery = std::make_unique<FrameDelivery>();
        delivery->stream = stream;

        if (copy_scaled_frame(
                *stream,
                spa_buffer->datas[0],
                delivery->width,
                delivery->height,
                delivery->rgba))
        {
            if (!stream->first_frame_delivered.exchange(true))
            {
                g_message(
                    "Live media thumbnail started: source=%ux%u; preview=%dx%d",
                    stream->video_info.size.width,
                    stream->video_info.size.height,
                    delivery->width,
                    delivery->height);
            }

            // A video preview is continuous UI work, not background idle
            // work. Idle delivery can be starved by Wayland and GTK events
            // after the first frame, making a healthy PipeWire stream look
            // frozen. Keep one frame pending, but dispatch it at normal main
            // context priority.
            g_main_context_invoke_full(
                nullptr,
                G_PRIORITY_DEFAULT,
                deliver_frame,
                delivery.release(),
                nullptr);
        }
        else
        {
            stream->delivery_pending = false;
        }
    }

    pw_stream_queue_buffer(stream->pipewire_stream, buffer);
}

void on_param_changed_with_owner(
    void *data,
    uint32_t id,
    const spa_pod *param)
{
    auto &owner = *static_cast<StreamCallbackData *>(data);
    const auto stream = owner.state.lock();

    if (stream)
        on_stream_param_changed(stream.get(), id, param);
}

const pw_stream_events PIPEWIRE_EVENTS = {
    PW_VERSION_STREAM_EVENTS,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    on_param_changed_with_owner,
    nullptr,
    nullptr,
    on_process_with_owner,
    nullptr,
    nullptr,
    nullptr};

} // namespace

struct DockWindowStreamProvider::Impl
{
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    zkde_screencast_unstable_v1 *screencast = nullptr;
    std::map<WindowId, std::shared_ptr<StreamState>> streams;
    std::map<WindowId, std::unique_ptr<StreamCallbackData>> callback_data;
    std::map<WindowId, std::unique_ptr<WaylandCallbackData>> wayland_callback_data;

    Impl();
    ~Impl();
    void stop(const WindowId &window_id);
    void stop_all();
    void start_pipewire(const std::shared_ptr<StreamState> &stream, uint32_t node_id);

    static void registry_global(
        void *data,
        wl_registry *registry,
        uint32_t name,
        const char *interface,
        uint32_t version);
    static void registry_removed(void *, wl_registry *, uint32_t) {}
    static void stream_created(void *data, zkde_screencast_stream_unstable_v1 *, uint32_t node_id);
    static void stream_closed(void *data, zkde_screencast_stream_unstable_v1 *);
    static void stream_failed(void *data, zkde_screencast_stream_unstable_v1 *, const char *error);
};

const wl_registry_listener REGISTRY_LISTENER = {
    DockWindowStreamProvider::Impl::registry_global,
    DockWindowStreamProvider::Impl::registry_removed};

const zkde_screencast_stream_unstable_v1_listener STREAM_LISTENER = {
    DockWindowStreamProvider::Impl::stream_closed,
    DockWindowStreamProvider::Impl::stream_created,
    DockWindowStreamProvider::Impl::stream_failed};
DockWindowStreamProvider::Impl::Impl()
{
    auto *gdk_display = gdk_display_get_default();

    if (!gdk_display || !GDK_IS_WAYLAND_DISPLAY(gdk_display))
        return;

    display = gdk_wayland_display_get_wl_display(gdk_display);
    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &REGISTRY_LISTENER, this);
    wl_display_roundtrip(display);
}

DockWindowStreamProvider::Impl::~Impl()
{
    stop_all();

    if (screencast)
        zkde_screencast_unstable_v1_destroy(screencast);
    if (registry)
        wl_registry_destroy(registry);
}

void DockWindowStreamProvider::Impl::registry_global(
    void *data,
    wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version)
{
    auto &self = *static_cast<Impl *>(data);

    if (std::strcmp(
            interface,
            zkde_screencast_unstable_v1_interface.name) == 0)
    {
        self.screencast = static_cast<zkde_screencast_unstable_v1 *>(
            wl_registry_bind(
                registry,
                name,
                &zkde_screencast_unstable_v1_interface,
                std::min(version, 5u)));
    }
}

void DockWindowStreamProvider::Impl::start_pipewire(
    const std::shared_ptr<StreamState> &stream,
    uint32_t node_id)
{
    auto callback = std::make_unique<StreamCallbackData>();
    callback->state = stream;

    stream->loop = pw_thread_loop_new("docklight-window-preview", nullptr);

    if (!stream->loop)
        return;

    stream->pipewire_stream = pw_stream_new_simple(
        pw_thread_loop_get_loop(stream->loop),
        "DockLight window preview",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Video",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_MEDIA_ROLE, "Screen",
            nullptr),
        &PIPEWIRE_EVENTS,
        callback.get());

    if (!stream->pipewire_stream)
        return;

    callback_data[stream->window_id] = std::move(callback);

    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod *params[1];
    const spa_rectangle preferred_size = SPA_RECTANGLE(
        static_cast<uint32_t>(stream->target_width),
        static_cast<uint32_t>(stream->target_height));
    const spa_rectangle minimum_size = SPA_RECTANGLE(1, 1);
    const spa_rectangle maximum_size = SPA_RECTANGLE(8192, 8192);
    const spa_fraction variable_rate = SPA_FRACTION(0, 1);
    const spa_fraction preferred_rate = SPA_FRACTION(30, 1);
    const spa_fraction minimum_rate = SPA_FRACTION(1, 1);
    const spa_fraction maximum_rate = SPA_FRACTION(60, 1);

    params[0] = static_cast<const spa_pod *>(
        spa_pod_builder_add_object(
            &builder,
            SPA_TYPE_OBJECT_Format,
            SPA_PARAM_EnumFormat,
            SPA_FORMAT_mediaType,
            SPA_POD_Id(SPA_MEDIA_TYPE_video),
            SPA_FORMAT_mediaSubtype,
            SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
            SPA_FORMAT_VIDEO_format,
            SPA_POD_CHOICE_ENUM_Id(
                4,
                SPA_VIDEO_FORMAT_BGRA,
                SPA_VIDEO_FORMAT_BGRx,
                SPA_VIDEO_FORMAT_RGBA,
                SPA_VIDEO_FORMAT_RGBx),
            SPA_FORMAT_VIDEO_size,
            SPA_POD_CHOICE_RANGE_Rectangle(
                &preferred_size,
                &minimum_size,
                &maximum_size),
            SPA_FORMAT_VIDEO_framerate,
            SPA_POD_Fraction(&variable_rate),
            SPA_FORMAT_VIDEO_maxFramerate,
            SPA_POD_CHOICE_RANGE_Fraction(
                &preferred_rate,
                &minimum_rate,
                &maximum_rate)));

    const int result = pw_stream_connect(
        stream->pipewire_stream,
        PW_DIRECTION_INPUT,
        node_id,
        static_cast<pw_stream_flags>(
            PW_STREAM_FLAG_AUTOCONNECT |
            PW_STREAM_FLAG_MAP_BUFFERS),
        params,
        1);

    if (result < 0 || pw_thread_loop_start(stream->loop) < 0)
        return;
}

void DockWindowStreamProvider::Impl::stream_created(
    void *data,
    zkde_screencast_stream_unstable_v1 *,
    uint32_t node_id)
{
    auto &callback = *static_cast<WaylandCallbackData *>(data);
    auto *provider = static_cast<Impl *>(callback.provider);
    const auto stream = callback.state.lock();

    if (provider && stream && stream->alive)
        provider->start_pipewire(stream, node_id);
}

void DockWindowStreamProvider::Impl::stream_closed(
    void *data,
    zkde_screencast_stream_unstable_v1 *)
{
    auto &callback = *static_cast<WaylandCallbackData *>(data);
    const auto stream = callback.state.lock();

    if (stream)
        stream->alive = false;
}

void DockWindowStreamProvider::Impl::stream_failed(
    void *data,
    zkde_screencast_stream_unstable_v1 *,
    const char *error)
{
    auto &callback = *static_cast<WaylandCallbackData *>(data);
    const auto stream = callback.state.lock();

    if (stream)
        stream->alive = false;
    g_warning("KWin window stream failed: %s", error ? error : "unknown error");
}

void DockWindowStreamProvider::Impl::stop(const WindowId &window_id)
{
    const auto found = streams.find(window_id);

    if (found == streams.end())
        return;

    const auto stream = found->second;
    stream->alive = false;

    if (stream->loop)
        pw_thread_loop_stop(stream->loop);
    if (stream->pipewire_stream)
        pw_stream_destroy(stream->pipewire_stream);
    if (stream->loop)
        pw_thread_loop_destroy(stream->loop);
    if (stream->wayland_stream)
        zkde_screencast_stream_unstable_v1_close(stream->wayland_stream);

    callback_data.erase(window_id);
    wayland_callback_data.erase(window_id);
    streams.erase(found);
}

void DockWindowStreamProvider::Impl::stop_all()
{
    while (!streams.empty())
        stop(streams.begin()->first);
}

DockWindowStreamProvider::DockWindowStreamProvider()
{
    pw_init(nullptr, nullptr);
    m_impl = std::make_unique<Impl>();
}

DockWindowStreamProvider::~DockWindowStreamProvider()
{
    m_impl.reset();
    pw_deinit();
}

bool DockWindowStreamProvider::available() const
{
    return m_impl && m_impl->screencast;
}

bool DockWindowStreamProvider::start(
    const WindowId &window_id,
    int target_width,
    int target_height,
    Callback callback)
{
    if (!available() || window_id.empty() ||
        target_width <= 0 || target_height <= 0)
    {
        return false;
    }

    stop(window_id);

    auto stream = std::make_shared<StreamState>();
    stream->window_id = window_id;
    stream->target_width = target_width;
    stream->target_height = target_height;
    stream->callback = std::move(callback);
    stream->wayland_stream =
        zkde_screencast_unstable_v1_stream_window(
            m_impl->screencast,
            window_id.c_str(),
            ZKDE_SCREENCAST_UNSTABLE_V1_POINTER_HIDDEN);

    if (!stream->wayland_stream)
        return false;

    // The listener needs both the stream and its provider to start PipeWire.
    // Store first, then attach a provider-bearing callback record.
    m_impl->streams[window_id] = stream;
    auto listener_data = std::make_unique<WaylandCallbackData>();
    listener_data->provider = m_impl.get();
    listener_data->state = stream;
    auto *listener_data_pointer = listener_data.get();
    m_impl->wayland_callback_data[window_id] =
        std::move(listener_data);
    zkde_screencast_stream_unstable_v1_add_listener(
        stream->wayland_stream,
        &STREAM_LISTENER,
        listener_data_pointer);
    wl_display_flush(m_impl->display);
    return true;
}

void DockWindowStreamProvider::stop(const WindowId &window_id)
{
    if (m_impl)
        m_impl->stop(window_id);
}

void DockWindowStreamProvider::stop_all()
{
    if (m_impl)
        m_impl->stop_all();
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
