// ------------------------------------------------------------
// Docklight 6.0
//
// Captures a Hyprland toplevel into wl_shm with
// ext-image-copy-capture-v1. Each request owns its Wayland connection so the
// GTK display connection is never used from a thumbnail worker thread.
// ------------------------------------------------------------

#include "hyprland_thumbnail_capture.h"

#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"

#include <wayland-client.h>

#include <glib.h>

#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{

struct Toplevel
{
    ext_foreign_toplevel_handle_v1 *handle = nullptr;
    std::string identifier;
    bool closed = false;
};

struct CaptureContext
{
    wl_display *display = nullptr;
    wl_registry *registry = nullptr;
    wl_shm *shm = nullptr;
    ext_foreign_toplevel_list_v1 *toplevel_list = nullptr;
    ext_foreign_toplevel_image_capture_source_manager_v1 *source_manager = nullptr;
    ext_image_copy_capture_manager_v1 *capture_manager = nullptr;
    std::vector<std::unique_ptr<Toplevel>> toplevels;

    ~CaptureContext()
    {
        if (display)
            wl_display_disconnect(display);
    }
};

struct SessionState
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint32_t> formats;
    bool constraints_ready = false;
    bool stopped = false;
};

struct FrameState
{
    bool complete = false;
    bool ready = false;
    uint32_t transform = WL_OUTPUT_TRANSFORM_NORMAL;
};

void handle_closed(
    void *data,
    ext_foreign_toplevel_handle_v1 *)
{
    static_cast<Toplevel *>(data)->closed = true;
}

void handle_done(void *, ext_foreign_toplevel_handle_v1 *) {}
void handle_title(void *, ext_foreign_toplevel_handle_v1 *, const char *) {}
void handle_app_id(void *, ext_foreign_toplevel_handle_v1 *, const char *) {}

void handle_identifier(
    void *data,
    ext_foreign_toplevel_handle_v1 *,
    const char *identifier)
{
    static_cast<Toplevel *>(data)->identifier =
        identifier ? identifier : "";
}

const ext_foreign_toplevel_handle_v1_listener TOPLEVEL_HANDLE_LISTENER = {
    handle_closed,
    handle_done,
    handle_title,
    handle_app_id,
    handle_identifier};

void list_toplevel(
    void *data,
    ext_foreign_toplevel_list_v1 *,
    ext_foreign_toplevel_handle_v1 *handle)
{
    auto *context = static_cast<CaptureContext *>(data);
    auto entry = std::make_unique<Toplevel>();
    entry->handle = handle;
    ext_foreign_toplevel_handle_v1_add_listener(
        handle,
        &TOPLEVEL_HANDLE_LISTENER,
        entry.get());
    context->toplevels.push_back(std::move(entry));
}

void list_finished(void *, ext_foreign_toplevel_list_v1 *) {}

const ext_foreign_toplevel_list_v1_listener TOPLEVEL_LIST_LISTENER = {
    list_toplevel,
    list_finished};

void registry_global(
    void *data,
    wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version)
{
    auto *context = static_cast<CaptureContext *>(data);
    if (std::strcmp(interface, wl_shm_interface.name) == 0)
    {
        context->shm = static_cast<wl_shm *>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
    }
    else if (std::strcmp(
                 interface,
                 ext_foreign_toplevel_list_v1_interface.name) == 0)
    {
        context->toplevel_list =
            static_cast<ext_foreign_toplevel_list_v1 *>(
                wl_registry_bind(
                    registry,
                    name,
                    &ext_foreign_toplevel_list_v1_interface,
                    std::min(version, 1U)));
        ext_foreign_toplevel_list_v1_add_listener(
            context->toplevel_list,
            &TOPLEVEL_LIST_LISTENER,
            context);
    }
    else if (std::strcmp(
                 interface,
                 ext_foreign_toplevel_image_capture_source_manager_v1_interface.name) == 0)
    {
        context->source_manager =
            static_cast<ext_foreign_toplevel_image_capture_source_manager_v1 *>(
                wl_registry_bind(
                    registry,
                    name,
                    &ext_foreign_toplevel_image_capture_source_manager_v1_interface,
                    std::min(version, 1U)));
    }
    else if (std::strcmp(
                 interface,
                 ext_image_copy_capture_manager_v1_interface.name) == 0)
    {
        context->capture_manager =
            static_cast<ext_image_copy_capture_manager_v1 *>(
                wl_registry_bind(
                    registry,
                    name,
                    &ext_image_copy_capture_manager_v1_interface,
                    std::min(version, 1U)));
    }
}

void registry_global_remove(void *, wl_registry *, uint32_t) {}

const wl_registry_listener REGISTRY_LISTENER = {
    registry_global,
    registry_global_remove};

void session_buffer_size(
    void *data,
    ext_image_copy_capture_session_v1 *,
    uint32_t width,
    uint32_t height)
{
    auto *state = static_cast<SessionState *>(data);
    state->width = width;
    state->height = height;
}

void session_shm_format(
    void *data,
    ext_image_copy_capture_session_v1 *,
    uint32_t format)
{
    static_cast<SessionState *>(data)->formats.push_back(format);
}

void session_dmabuf_device(
    void *,
    ext_image_copy_capture_session_v1 *,
    wl_array *)
{
}

void session_dmabuf_format(
    void *,
    ext_image_copy_capture_session_v1 *,
    uint32_t,
    wl_array *)
{
}

void session_done(
    void *data,
    ext_image_copy_capture_session_v1 *)
{
    static_cast<SessionState *>(data)->constraints_ready = true;
}

void session_stopped(
    void *data,
    ext_image_copy_capture_session_v1 *)
{
    static_cast<SessionState *>(data)->stopped = true;
}

const ext_image_copy_capture_session_v1_listener SESSION_LISTENER = {
    session_buffer_size,
    session_shm_format,
    session_dmabuf_device,
    session_dmabuf_format,
    session_done,
    session_stopped};

void frame_transform(
    void *data,
    ext_image_copy_capture_frame_v1 *,
    uint32_t transform)
{
    static_cast<FrameState *>(data)->transform = transform;
}

void frame_damage(
    void *,
    ext_image_copy_capture_frame_v1 *,
    int32_t,
    int32_t,
    int32_t,
    int32_t)
{
}

void frame_presentation_time(
    void *,
    ext_image_copy_capture_frame_v1 *,
    uint32_t,
    uint32_t,
    uint32_t)
{
}

void frame_ready(
    void *data,
    ext_image_copy_capture_frame_v1 *)
{
    auto *state = static_cast<FrameState *>(data);
    state->ready = true;
    state->complete = true;
}

void frame_failed(
    void *data,
    ext_image_copy_capture_frame_v1 *,
    uint32_t)
{
    static_cast<FrameState *>(data)->complete = true;
}

const ext_image_copy_capture_frame_v1_listener FRAME_LISTENER = {
    frame_transform,
    frame_damage,
    frame_presentation_time,
    frame_ready,
    frame_failed};

bool dispatch_until(
    wl_display *display,
    const bool &condition,
    int maximum_roundtrips = 4)
{
    for (int attempt = 0;
         !condition && attempt < maximum_roundtrips;
         ++attempt)
    {
        if (wl_display_roundtrip(display) < 0)
            return false;
    }
    return condition;
}

bool wait_for_frame(
    wl_display *display,
    const bool &complete,
    int timeout_ms = 1500)
{
    if (wl_display_flush(display) < 0 && errno != EAGAIN)
        return false;

    const auto deadline =
        g_get_monotonic_time() +
        static_cast<gint64>(timeout_ms) * 1000;
    while (!complete)
    {
        const auto remaining_us =
            deadline - g_get_monotonic_time();
        if (remaining_us <= 0)
            return false;

        pollfd descriptor = {
            wl_display_get_fd(display),
            POLLIN,
            0};
        const int remaining_ms = static_cast<int>(
            std::max<gint64>(
                1,
                (remaining_us + 999) / 1000));
        int result = 0;
        do
        {
            result = poll(&descriptor, 1, remaining_ms);
        }
        while (result < 0 && errno == EINTR);

        if (result <= 0 ||
            (descriptor.revents &
             (POLLERR | POLLHUP | POLLNVAL)) != 0 ||
            wl_display_dispatch(display) < 0)
        {
            return false;
        }
    }
    return true;
}

int create_shm_file(std::size_t size)
{
    char *path = nullptr;
    GError *error = nullptr;
    const int fd = g_file_open_tmp(
        "docklight-thumbnail-XXXXXX",
        &path,
        &error);
    if (fd >= 0)
    {
        unlink(path);
        if (ftruncate(fd, static_cast<off_t>(size)) != 0)
        {
            close(fd);
            g_free(path);
            g_clear_error(&error);
            return -1;
        }
    }
    g_free(path);
    g_clear_error(&error);
    return fd;
}

std::optional<uint32_t> preferred_format(
    const std::vector<uint32_t> &formats)
{
    // Preview cards are always composited onto an opaque background. Prefer
    // XRGB so compositor implementations which leave alpha undefined in an
    // otherwise valid toplevel export cannot turn the captured image fully
    // transparent. Keep alpha formats as compatibility fallbacks.
    constexpr uint32_t preference[] = {
        WL_SHM_FORMAT_XRGB8888,
        WL_SHM_FORMAT_XBGR8888,
        WL_SHM_FORMAT_ARGB8888,
        WL_SHM_FORMAT_ABGR8888};

    for (const auto candidate : preference)
    {
        if (std::find(
                formats.begin(),
                formats.end(),
                candidate) != formats.end())
        {
            return candidate;
        }
    }
    return std::nullopt;
}

void convert_to_rgba(
    HyprlandThumbnail &thumbnail,
    const void *pixels,
    uint32_t format)
{
    const auto pixel_count =
        static_cast<std::size_t>(thumbnail.width) *
        thumbnail.height;
    const auto *source = static_cast<const uint32_t *>(pixels);
    thumbnail.rgba.resize(pixel_count * 4);

    const bool blue_first =
        format == WL_SHM_FORMAT_ABGR8888 ||
        format == WL_SHM_FORMAT_XBGR8888;
    const bool has_alpha =
        format == WL_SHM_FORMAT_ARGB8888 ||
        format == WL_SHM_FORMAT_ABGR8888;

    for (std::size_t index = 0; index < pixel_count; ++index)
    {
        const uint32_t pixel = source[index];
        const auto first = static_cast<unsigned char>(pixel >> 16U);
        const auto second = static_cast<unsigned char>(pixel >> 8U);
        const auto third = static_cast<unsigned char>(pixel);
        thumbnail.rgba[index * 4] = blue_first ? third : first;
        thumbnail.rgba[index * 4 + 1] = second;
        thumbnail.rgba[index * 4 + 2] = blue_first ? first : third;
        thumbnail.rgba[index * 4 + 3] = has_alpha
            ? static_cast<unsigned char>(pixel >> 24U)
            : 255;
    }
}

} // namespace

std::optional<HyprlandThumbnail>
capture_hyprland_toplevel(const WindowId &window_id)
{
    CaptureContext context;
    context.display = wl_display_connect(nullptr);
    if (!context.display)
        return std::nullopt;

    context.registry = wl_display_get_registry(context.display);
    if (!context.registry)
        return std::nullopt;

    wl_registry_add_listener(
        context.registry,
        &REGISTRY_LISTENER,
        &context);
    if (wl_display_roundtrip(context.display) < 0 ||
        !context.shm ||
        !context.toplevel_list ||
        !context.source_manager ||
        !context.capture_manager ||
        wl_display_roundtrip(context.display) < 0)
    {
        return std::nullopt;
    }

    const auto found = std::find_if(
        context.toplevels.begin(),
        context.toplevels.end(),
        [&window_id](const auto &entry)
        {
            return !entry->closed &&
                   entry->identifier == window_id;
        });
    if (found == context.toplevels.end())
        return std::nullopt;

    auto *source =
        ext_foreign_toplevel_image_capture_source_manager_v1_create_source(
            context.source_manager,
            (*found)->handle);
    if (!source)
        return std::nullopt;

    auto *session =
        ext_image_copy_capture_manager_v1_create_session(
            context.capture_manager,
            source,
            0);
    if (!session)
        return std::nullopt;

    SessionState session_state;
    ext_image_copy_capture_session_v1_add_listener(
        session,
        &SESSION_LISTENER,
        &session_state);
    if (!dispatch_until(
            context.display,
            session_state.constraints_ready) ||
        session_state.stopped ||
        session_state.width == 0 ||
        session_state.height == 0)
    {
        return std::nullopt;
    }

    const auto format = preferred_format(session_state.formats);
    if (!format ||
        session_state.width >
            static_cast<uint32_t>(std::numeric_limits<int>::max()) ||
        session_state.height >
            static_cast<uint32_t>(std::numeric_limits<int>::max()))
    {
        return std::nullopt;
    }

    const std::size_t stride =
        static_cast<std::size_t>(session_state.width) * 4;
    if (session_state.height >
        std::numeric_limits<std::size_t>::max() / stride)
    {
        return std::nullopt;
    }
    const std::size_t size = stride * session_state.height;
    if (stride >
            static_cast<std::size_t>(
                std::numeric_limits<int32_t>::max()) ||
        size >
            static_cast<std::size_t>(
                std::numeric_limits<int32_t>::max()))
    {
        return std::nullopt;
    }

    const int fd = create_shm_file(size);
    if (fd < 0)
        return std::nullopt;

    void *pixels = mmap(
        nullptr,
        size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0);
    if (pixels == MAP_FAILED)
    {
        close(fd);
        return std::nullopt;
    }

    auto *pool = wl_shm_create_pool(
        context.shm,
        fd,
        static_cast<int32_t>(size));
    if (!pool)
    {
        munmap(pixels, size);
        close(fd);
        return std::nullopt;
    }

    auto *buffer = wl_shm_pool_create_buffer(
        pool,
        0,
        static_cast<int32_t>(session_state.width),
        static_cast<int32_t>(session_state.height),
        static_cast<int32_t>(stride),
        *format);
    wl_shm_pool_destroy(pool);
    if (!buffer)
    {
        munmap(pixels, size);
        close(fd);
        return std::nullopt;
    }

    FrameState frame_state;
    auto *frame =
        ext_image_copy_capture_session_v1_create_frame(session);
    if (!frame)
    {
        wl_buffer_destroy(buffer);
        munmap(pixels, size);
        close(fd);
        return std::nullopt;
    }
    ext_image_copy_capture_frame_v1_add_listener(
        frame,
        &FRAME_LISTENER,
        &frame_state);
    ext_image_copy_capture_frame_v1_attach_buffer(frame, buffer);
    ext_image_copy_capture_frame_v1_damage_buffer(
        frame,
        0,
        0,
        static_cast<int32_t>(session_state.width),
        static_cast<int32_t>(session_state.height));
    ext_image_copy_capture_frame_v1_capture(frame);

    const bool captured = wait_for_frame(
        context.display,
        frame_state.complete) && frame_state.ready;

    HyprlandThumbnail thumbnail;
    if (captured)
    {
        thumbnail.width = static_cast<int>(session_state.width);
        thumbnail.height = static_cast<int>(session_state.height);
        convert_to_rgba(thumbnail, pixels, *format);
    }

    ext_image_copy_capture_frame_v1_destroy(frame);
    wl_buffer_destroy(buffer);
    munmap(pixels, size);
    close(fd);

    if (!captured)
        return std::nullopt;
    return thumbnail;
}
