// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_window_thumbnail_provider.cpp
//
// Implementation overview:
// Implements asynchronous static thumbnail capture through native X11,
// KWin's ScreenShot2 interface, or a GNOME Shell compositor snapshot.
//
// Important implementation decisions:
// - D-Bus requests do not block the GTK main loop.
// - Returned file descriptors and pixel buffers are validated before
//   conversion.
// - Completion checks shared lifetime state before invoking callbacks.
//
// ------------------------------------------------------------

#include "dock_window_thumbnail_provider.h"
#include "integrations/desktop_session_identity.h"

#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gdkmm/pixbufloader.h>
#include <gio/gio.h>
#include <glib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <mutex>
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
constexpr char GNOME_THUMBNAIL_SERVICE[] =
    "org.docklight6.GnomeThumbnailer";
constexpr char GNOME_THUMBNAIL_PATH[] =
    "/org/docklight6/GnomeThumbnailer";
constexpr char GNOME_THUMBNAIL_INTERFACE[] =
    "org.docklight6.GnomeThumbnailer1";

struct Completion
{
    std::weak_ptr<DockWindowThumbnailProvider::State> state;
    WindowId window_id;
    int source_width = 0;
    int source_height = 0;
    int target_width = 0;
    int target_height = 0;
    double x11_oversample = 2.0;
    bool x11_native_capture = false;
    bool x11_strict_composite = false;
    std::vector<unsigned char> rgba;
    std::vector<unsigned char> encoded_image;
    DockWindowThumbnailProvider::Callback callback;
};

struct LivePreviewsCompletion
{
    std::weak_ptr<DockWindowThumbnailProvider::State> state;
    DockWindowThumbnailProvider::LivePreviewsCallback callback;
};

void complete_gnome_live_previews(
    GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
    std::unique_ptr<LivePreviewsCompletion> completion(
        static_cast<LivePreviewsCompletion *>(user_data));
    GError *error = nullptr;
    auto *reply = g_dbus_connection_call_finish(
        G_DBUS_CONNECTION(source),
        result,
        &error);
    const bool success = reply != nullptr;

    if (reply)
        g_variant_unref(reply);

    const auto state = completion->state.lock();
    if (state && state->alive && completion->callback)
        completion->callback(success);

    g_clear_error(&error);
}

std::mutex x_error_handler_mutex;
thread_local bool x_capture_error = false;

unsigned long parsed_x11_window_id(
    const WindowId &window_id)
{
    char *end = nullptr;
    errno = 0;
    const auto parsed = std::strtoull(
        window_id.c_str(),
        &end,
        0);

    if (errno != 0 ||
        !end ||
        *end != '\0' ||
        parsed == 0 ||
        parsed >
            std::numeric_limits<unsigned long>::max())
    {
        return None;
    }

    return static_cast<unsigned long>(parsed);
}

bool is_x11_window_id(
    const WindowId &window_id)
{
    return parsed_x11_window_id(window_id) != None;
}

int capture_x_error_handler(
    Display *,
    XErrorEvent *)
{
    x_capture_error = true;
    return 0;
}

bool x11_compositor_owns_screen(
    Display *display,
    Screen *screen)
{
    if (!display || !screen)
        return false;

    const std::string selection_name =
        "_NET_WM_CM_S" +
        std::to_string(XScreenNumberOfScreen(screen));
    const Atom selection = XInternAtom(
        display,
        selection_name.c_str(),
        False);

    return selection != None &&
           XGetSelectionOwner(display, selection) != None;
}

struct PixelChannel
{
    unsigned long mask = 0;
    unsigned long maximum = 0;
    unsigned int shift = 0;
};

PixelChannel pixel_channel(
    unsigned long mask)
{
    if (mask == 0)
        return {};

    unsigned int shift = 0;
    while (((mask >> shift) & 1UL) == 0UL)
        ++shift;

    return {
        mask,
        mask >> shift,
        shift};
}

unsigned char pixel_channel(
    unsigned long pixel,
    const PixelChannel &channel)
{
    if (channel.mask == 0 ||
        channel.maximum == 0)
    {
        return 0;
    }

    const unsigned long value =
        (pixel & channel.mask) >> channel.shift;

    if (channel.maximum == 255UL)
        return static_cast<unsigned char>(value);

    return static_cast<unsigned char>(
        (value * 255UL + channel.maximum / 2UL) /
        channel.maximum);
}

unsigned long image_pixel(
    XImage *image,
    int x,
    int y)
{
    // XGetPixel dispatches through a function pointer for every pixel. The
    // XRender destination used here is normally 32 bpp, so read that common
    // layout directly; retain XGetPixel for unusual visuals.
    if (image && image->bits_per_pixel == 32)
    {
        std::uint32_t value = 0;
        std::memcpy(
            &value,
            image->data +
                static_cast<std::size_t>(y) * image->bytes_per_line +
                static_cast<std::size_t>(x) * 4,
            sizeof(value));

#if G_BYTE_ORDER == G_BIG_ENDIAN
        if (image->byte_order == LSBFirst)
#else
        if (image->byte_order == MSBFirst)
#endif
        {
            value = GUINT32_SWAP_LE_BE(value);
        }

        return value;
    }

    return XGetPixel(image, x, y);
}

bool capture_x11_window(
    Completion &completion)
{
    char *end = nullptr;
    errno = 0;
    const auto parsed = std::strtoull(
        completion.window_id.c_str(),
        &end,
        0);

    if (errno != 0 ||
        !end ||
        *end != '\0' ||
        parsed == 0 ||
        parsed > std::numeric_limits<unsigned long>::max())
    {
        return false;
    }

    Display *display = XOpenDisplay(nullptr);
    if (!display)
        return false;

    const ::Window window =
        static_cast<::Window>(parsed);
    XWindowAttributes attributes{};
    Pixmap pixmap = None;
    Pixmap scaled_pixmap = None;
    Picture source_picture = None;
    Picture scaled_picture = None;
    XImage *image = nullptr;
    bool success = false;

    // X errors are asynchronous. Serialize the process-global handler while
    // probing a client that may disappear between the registry snapshot and
    // this worker, and synchronize before restoring the previous handler.
    std::lock_guard<std::mutex> guard(
        x_error_handler_mutex);
    auto previous_handler = XSetErrorHandler(
        capture_x_error_handler);
    x_capture_error = false;

    if (XGetWindowAttributes(
            display,
            window,
            &attributes) &&
        (!completion.x11_strict_composite ||
         (attributes.map_state == IsViewable &&
          attributes.c_class == InputOutput)) &&
        attributes.width > 0 &&
        attributes.height > 0)
    {
        int event_base = 0;
        int error_base = 0;
        const bool composite_available =
            XCompositeQueryExtension(
                display,
                &event_base,
                &error_base);
        // Xfwm can expose the Composite extension while its compositor is
        // disabled. NameWindowPixmap may still succeed in that state, but
        // Firefox PiP can leave the returned pixmap at its first frame. A
        // guarded native capture was explicitly requested for these mapped
        // media windows, so prefer the live window drawable when no screen
        // compositor owns the standard selection.
        const bool prefer_native_drawable =
            completion.x11_native_capture &&
            !x11_compositor_owns_screen(
                display,
                attributes.screen);
        if (composite_available &&
            !prefer_native_drawable)
        {
            pixmap = XCompositeNameWindowPixmap(
                display,
                window);
            XSync(display, False);

            if (completion.x11_strict_composite &&
                x_capture_error)
                pixmap = None;
        }

        int drawable_width = attributes.width;
        int drawable_height = attributes.height;

        if (pixmap != None && !x_capture_error)
        {
            ::Window root = None;
            int x = 0;
            int y = 0;
            unsigned int width = 0;
            unsigned int height = 0;
            unsigned int border_width = 0;
            unsigned int depth = 0;

            x_capture_error = false;
            const bool has_pixmap_geometry =
                XGetGeometry(
                    display,
                    pixmap,
                    &root,
                    &x,
                    &y,
                    &width,
                    &height,
                    &border_width,
                    &depth) != 0;
            XSync(display, False);

            if (has_pixmap_geometry &&
                !x_capture_error &&
                ((!completion.x11_strict_composite &&
                  width > 0 &&
                  height > 0) ||
                 (completion.x11_strict_composite &&
                  width == static_cast<unsigned int>(
                               attributes.width) &&
                  height == static_cast<unsigned int>(
                                attributes.height))))
            {
                drawable_width =
                    static_cast<int>(width);
                drawable_height =
                    static_cast<int>(height);
            }
            else
            {
                XFreePixmap(display, pixmap);
                pixmap = None;
            }
        }

        // When Composite is available, never fall back to reading the window
        // drawable after NameWindowPixmap fails in strict mode. During
        // map/unmap transitions, or without a compositor-owned redirection,
        // that drawable can expose stale or partially obscured storage.
        const bool named_pixmap_valid =
            pixmap != None && !x_capture_error;
        const bool drawable_valid =
            !completion.x11_strict_composite ||
            named_pixmap_valid ||
            completion.x11_native_capture;

        Drawable drawable =
            named_pixmap_valid
                ? pixmap
                : window;
        x_capture_error = false;

        int capture_width = drawable_width;
        int capture_height = drawable_height;
        Visual *capture_visual = attributes.visual;

        // Scale the composited window on the X server before reading pixels
        // back. The old path transferred and converted every source pixel on
        // every frame, which made video previews CPU-bound. XRender keeps the
        // large image in the compositor/X server and transfers only the small
        // preview-sized result.
        int render_event_base = 0;
        int render_error_base = 0;
        if (drawable_valid &&
            !prefer_native_drawable &&
            (!completion.x11_native_capture ||
             completion.x11_strict_composite) &&
            completion.target_width > 0 &&
            completion.target_height > 0 &&
            XRenderQueryExtension(
                display,
                &render_event_base,
                &render_error_base))
        {
            // Keep a higher-resolution intermediate so the final GdkPixbuf
            // downscale has enough samples to preserve text and fine edges.
            // Never enlarge beyond the compositor's native window pixmap.
            const double scale = std::min({
                1.0,
                completion.x11_oversample *
                    completion.target_width /
                    drawable_width,
                completion.x11_oversample *
                    completion.target_height /
                    drawable_height});
            const int scaled_width = std::max(
                1,
                static_cast<int>(std::lround(
                    drawable_width * scale)));
            const int scaled_height = std::max(
                1,
                static_cast<int>(std::lround(
                    drawable_height * scale)));

            auto *source_format =
                XRenderFindVisualFormat(
                    display,
                    attributes.visual);
            auto *destination_visual =
                DefaultVisualOfScreen(
                    attributes.screen);
            auto *destination_format =
                XRenderFindVisualFormat(
                    display,
                    destination_visual);

            if (source_format && destination_format)
            {
                scaled_pixmap = XCreatePixmap(
                    display,
                    RootWindowOfScreen(attributes.screen),
                    static_cast<unsigned int>(scaled_width),
                    static_cast<unsigned int>(scaled_height),
                    static_cast<unsigned int>(
                        DefaultDepthOfScreen(
                            attributes.screen)));
                source_picture = XRenderCreatePicture(
                    display,
                    drawable,
                    source_format,
                    0,
                    nullptr);
                scaled_picture = XRenderCreatePicture(
                    display,
                    scaled_pixmap,
                    destination_format,
                    0,
                    nullptr);

                XTransform transform = {{
                    {XDoubleToFixed(
                         static_cast<double>(drawable_width) /
                         scaled_width),
                     XDoubleToFixed(0.0),
                     XDoubleToFixed(0.0)},
                    {XDoubleToFixed(0.0),
                     XDoubleToFixed(
                         static_cast<double>(drawable_height) /
                         scaled_height),
                     XDoubleToFixed(0.0)},
                    {XDoubleToFixed(0.0),
                     XDoubleToFixed(0.0),
                     XDoubleToFixed(1.0)}}};

                XRenderSetPictureTransform(
                    display,
                    source_picture,
                    &transform);
                XRenderSetPictureFilter(
                    display,
                    source_picture,
                    FilterBilinear,
                    nullptr,
                    0);

                // Hold the server only for the short XRender snapshot. Pixel
                // readback and conversion happen after other clients resume.
                XGrabServer(display);
                XRenderComposite(
                    display,
                    PictOpSrc,
                    source_picture,
                    None,
                    scaled_picture,
                    0,
                    0,
                    0,
                    0,
                    0,
                    0,
                    static_cast<unsigned int>(scaled_width),
                    static_cast<unsigned int>(scaled_height));
                XSync(display, False);
                XUngrabServer(display);
                XFlush(display);

                if (!x_capture_error)
                {
                    drawable = scaled_pixmap;
                    capture_width = scaled_width;
                    capture_height = scaled_height;
                    capture_visual = destination_visual;
                }
            }
        }

        x_capture_error = false;

        if (drawable_valid &&
            completion.x11_native_capture)
            XGrabServer(display);

        if (drawable_valid)
        {
            image = XGetImage(
                display,
                drawable,
                0,
                0,
                static_cast<unsigned int>(capture_width),
                static_cast<unsigned int>(capture_height),
                AllPlanes,
                ZPixmap);
            XSync(display, False);
        }

        if (drawable_valid &&
            completion.x11_native_capture)
        {
            XUngrabServer(display);
            XFlush(display);
        }

        bool image_is_valid = image && !x_capture_error;
        if (image_is_valid && completion.x11_strict_composite)
        {
            XWindowAttributes verified_attributes{};
            x_capture_error = false;
            const bool same_viewable_window =
                XGetWindowAttributes(
                    display,
                    window,
                    &verified_attributes) != 0;
            XSync(display, False);

            image_is_valid =
                !x_capture_error &&
                same_viewable_window &&
                verified_attributes.map_state == IsViewable &&
                verified_attributes.c_class == InputOutput &&
                verified_attributes.width == attributes.width &&
                verified_attributes.height == attributes.height &&
                verified_attributes.visual == attributes.visual;
        }

        if (image_is_valid)
        {
            completion.source_width =
                capture_width;
            completion.source_height =
                capture_height;
            completion.rgba.resize(
                static_cast<std::size_t>(
                    capture_width) *
                capture_height * 4);

            const auto red_channel =
                pixel_channel(
                capture_visual
                    ? capture_visual->red_mask
                    : image->red_mask);
            const auto green_channel =
                pixel_channel(
                capture_visual
                    ? capture_visual->green_mask
                    : image->green_mask);
            const auto blue_channel =
                pixel_channel(
                capture_visual
                    ? capture_visual->blue_mask
                    : image->blue_mask);

            for (int y = 0;
                 y < capture_height;
                 ++y)
            {
                for (int x = 0;
                     x < capture_width;
                     ++x)
                {
                    const auto pixel =
                        image_pixel(image, x, y);
                    const auto index =
                        (static_cast<std::size_t>(y) *
                             capture_width +
                         x) *
                        4;

                    completion.rgba[index] =
                        pixel_channel(pixel, red_channel);
                    completion.rgba[index + 1] =
                        pixel_channel(pixel, green_channel);
                    completion.rgba[index + 2] =
                        pixel_channel(pixel, blue_channel);
                    completion.rgba[index + 3] = 255;
                }
            }

            success = true;
        }
    }

    if (image)
        XDestroyImage(image);
    if (source_picture != None)
        XRenderFreePicture(display, source_picture);
    if (scaled_picture != None)
        XRenderFreePicture(display, scaled_picture);
    if (scaled_pixmap != None)
        XFreePixmap(display, scaled_pixmap);
    if (pixmap != None)
        XFreePixmap(display, pixmap);

    XSync(display, False);
    XSetErrorHandler(previous_handler);
    XCloseDisplay(display);

    if (!success)
        completion.rgba.clear();

    return success;
}

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

    if (!completion->encoded_image.empty())
    {
        try
        {
            auto loader = Gdk::PixbufLoader::create();
            loader->write(
                completion->encoded_image.data(),
                completion->encoded_image.size());
            loader->close();
            auto source = loader->get_pixbuf();

            if (source &&
                source->get_width() > 0 &&
                source->get_height() > 0)
            {
                const double scale = std::min({
                    1.0,
                    static_cast<double>(
                        completion->target_width) /
                        source->get_width(),
                    static_cast<double>(
                        completion->target_height) /
                        source->get_height()});
                const int scaled_width = std::max(
                    1,
                    static_cast<int>(std::lround(
                        source->get_width() * scale)));
                const int scaled_height = std::max(
                    1,
                    static_cast<int>(std::lround(
                        source->get_height() * scale)));

                thumbnail = source->scale_simple(
                    scaled_width,
                    scaled_height,
                    Gdk::INTERP_BILINEAR);
            }
        }
        catch (const Glib::Error &)
        {
            thumbnail.reset();
        }
    }
    else if (!completion->rgba.empty() &&
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

            // Match compositor thumbnail actors: fit inside the card while
            // preserving aspect ratio, but never enlarge a small source.
            const double scale = std::min({
                1.0,
                static_cast<double>(
                    completion->target_width) /
                    completion->source_width,
                static_cast<double>(
                    completion->target_height) /
                    completion->source_height});
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
    double x11_oversample,
    bool x11_native_capture,
    bool x11_strict_composite,
    DockWindowThumbnailProvider::Callback callback)
{
    auto completion =
        std::make_unique<Completion>();
    completion->state = std::move(state);
    completion->window_id = std::move(window_id);
    completion->target_width = target_width;
    completion->target_height = target_height;
    completion->x11_oversample =
        std::clamp(x11_oversample, 1.0, 2.0);
    completion->x11_native_capture =
        x11_native_capture;
    completion->x11_strict_composite =
        x11_strict_composite;
    completion->callback = std::move(callback);

    // Hold the provider state for the entire capture so its shared session
    // connection cannot be released while this worker is using it.
    const auto active_state =
        completion->state.lock();

    if (!active_state ||
        !active_state->alive)
    {
        schedule_delivery(std::move(completion));
        return;
    }

    if (active_state->gnome_shell_capture &&
        active_state->connection)
    {
        GError *error = nullptr;
        auto *reply = g_dbus_connection_call_sync(
            active_state->connection,
            GNOME_THUMBNAIL_SERVICE,
            GNOME_THUMBNAIL_PATH,
            GNOME_THUMBNAIL_INTERFACE,
            "CaptureWindow",
            g_variant_new(
                "(sii)",
                completion->window_id.c_str(),
                completion->target_width,
                completion->target_height),
            G_VARIANT_TYPE("(ay)"),
            G_DBUS_CALL_FLAGS_NONE,
            5000,
            nullptr,
            &error);

        if (reply)
        {
            GVariant *encoded = nullptr;
            g_variant_get(reply, "(@ay)", &encoded);
            gsize size = 0;
            const auto *bytes =
                static_cast<const unsigned char *>(
                    g_variant_get_fixed_array(
                        encoded,
                        &size,
                        sizeof(unsigned char)));

            if (bytes && size > 0)
            {
                completion->encoded_image.assign(
                    bytes,
                    bytes + size);
            }

            g_variant_unref(encoded);
            g_variant_unref(reply);
        }
        else if (error &&
                 !g_error_matches(
                     error,
                     G_DBUS_ERROR,
                     G_DBUS_ERROR_SERVICE_UNKNOWN))
        {
            g_warning(
                "Cannot capture GNOME window thumbnail: %s",
                error->message);
        }

        g_clear_error(&error);
        schedule_delivery(std::move(completion));
        return;
    }

    if (active_state->x11 &&
        is_x11_window_id(completion->window_id))
    {
        capture_x11_window(*completion);
        schedule_delivery(std::move(completion));
        return;
    }

    if (!active_state->connection)
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
    auto display = gdk_display_get_default();
    m_state->x11 =
        display && GDK_IS_X11_DISPLAY(display);

    const char *desktop = std::getenv("XDG_CURRENT_DESKTOP");
    if (!desktop || !*desktop)
        desktop = std::getenv("XDG_SESSION_DESKTOP");
    if (!desktop || !*desktop)
        desktop = std::getenv("DESKTOP_SESSION");

    const auto normalized_desktop =
        DesktopSessionIdentity::normalized(
            desktop ? desktop : "");

    // The GNOME Shell registry publishes Mutter stable-sequence ids on both
    // Wayland and X11. They can be numeric but are not X11 window handles.
    // Route them through the same Shell compositor capture service that owns
    // the registry so static and live previews address the correct actors.
    m_state->gnome_shell_capture =
        DesktopSessionIdentity::
            identifies_gnome_shell(
                normalized_desktop);

    // ScreenShot2 is a KWin-only API. On other Wayland compositors, avoid
    // connecting unless their own capture transport is active.
    const bool kwin_wayland =
        !m_state->x11 &&
        (normalized_desktop.find("kde") != std::string::npos ||
         normalized_desktop.find("plasma") != std::string::npos);
    if (!m_state->gnome_shell_capture &&
        !m_state->x11 &&
        !kwin_wayland)
        return;

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
    hide_gnome_live_previews();
    m_state->alive = false;
    set_x11_window_redirection(false);
}

void DockWindowThumbnailProvider::
    set_x11_window_redirection(
        bool enabled)
{
    if (!m_state || !m_state->x11)
        return;

    auto *redirect_display =
        static_cast<Display *>(
            m_x11_redirect_display);
    if (enabled == (redirect_display != nullptr))
        return;

    std::lock_guard<std::mutex> guard(
        x_error_handler_mutex);

    if (!enabled)
    {
        auto previous_handler = XSetErrorHandler(
            capture_x_error_handler);
        x_capture_error = false;
        for (const auto window :
             m_x11_redirected_windows)
        {
            XCompositeUnredirectWindow(
                redirect_display,
                window,
                CompositeRedirectAutomatic);
        }
        XSync(redirect_display, False);
        XSetErrorHandler(previous_handler);
        m_x11_redirected_windows.clear();
        XCloseDisplay(redirect_display);
        m_x11_redirect_display = nullptr;
        return;
    }

    redirect_display = XOpenDisplay(nullptr);
    if (!redirect_display)
    {
        g_warning(
            "Cannot open X11 display for window thumbnail redirection");
        return;
    }

    int event_base = 0;
    int error_base = 0;
    int major_version = 0;
    int minor_version = 0;
    const bool composite_available =
        XCompositeQueryExtension(
            redirect_display,
            &event_base,
            &error_base) &&
        XCompositeQueryVersion(
            redirect_display,
            &major_version,
            &minor_version) &&
        (major_version > 0 || minor_version >= 2);

    if (!composite_available)
    {
        XCloseDisplay(redirect_display);
        g_warning(
            "XComposite 0.2 is required for complete Openbox thumbnails");
        return;
    }

    m_x11_redirect_display = redirect_display;
}

void DockWindowThumbnailProvider::
    set_x11_redirected_windows(
        const std::vector<WindowId> &window_ids)
{
    auto *display =
        static_cast<Display *>(
            m_x11_redirect_display);
    if (!display)
        return;

    std::set<unsigned long> desired_windows;
    for (const auto &window_id : window_ids)
    {
        const auto window =
            parsed_x11_window_id(window_id);
        if (window != None)
            desired_windows.insert(window);
    }

    std::lock_guard<std::mutex> guard(
        x_error_handler_mutex);
    auto previous_handler = XSetErrorHandler(
        capture_x_error_handler);

    for (const auto window :
         m_x11_redirected_windows)
    {
        if (desired_windows.count(window) == 0)
        {
            x_capture_error = false;
            XCompositeUnredirectWindow(
                display,
                window,
                CompositeRedirectAutomatic);
            XSync(display, False);
        }
    }

    std::set<unsigned long> redirected_windows;
    for (const auto window : desired_windows)
    {
        if (m_x11_redirected_windows.count(window) != 0)
        {
            redirected_windows.insert(window);
            continue;
        }

        x_capture_error = false;
        XCompositeRedirectWindow(
            display,
            window,
            CompositeRedirectAutomatic);
        XSync(display, False);
        if (!x_capture_error)
            redirected_windows.insert(window);
    }

    XSetErrorHandler(previous_handler);
    m_x11_redirected_windows =
        std::move(redirected_windows);
}

bool DockWindowThumbnailProvider::
    supports_gnome_live_previews() const
{
    return m_state &&
           m_state->alive &&
           m_state->gnome_shell_capture &&
           m_state->connection;
}

void DockWindowThumbnailProvider::
    set_gnome_preview_color(
        double red,
        double green,
        double blue,
        double alpha)
{
    if (!supports_gnome_live_previews())
        return;

    // Keep this separate from ShowLivePreviews so either side can be upgraded
    // independently without breaking the established preview request.
    g_dbus_connection_call(
        m_state->connection,
        GNOME_THUMBNAIL_SERVICE,
        GNOME_THUMBNAIL_PATH,
        GNOME_THUMBNAIL_INTERFACE,
        "SetPreviewColor",
        g_variant_new(
            "(dddd)",
            std::clamp(red, 0.0, 1.0),
            std::clamp(green, 0.0, 1.0),
            std::clamp(blue, 0.0, 1.0),
            std::clamp(alpha, 0.0, 1.0)),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        nullptr,
        nullptr,
        nullptr);
}

void DockWindowThumbnailProvider::
    show_gnome_live_previews(
        const std::vector<GnomeLivePreviewRect>
            &previews,
        LivePreviewsCallback callback)
{
    if (!supports_gnome_live_previews() ||
        previews.empty())
    {
        if (callback)
            callback(false);
        return;
    }

    GVariantBuilder builder;
    g_variant_builder_init(
        &builder,
        G_VARIANT_TYPE("a(siiii)"));
    bool has_preview = false;

    for (const auto &preview : previews)
    {
        if (preview.window_id.empty() ||
            preview.width <= 0 ||
            preview.height <= 0)
        {
            continue;
        }

        g_variant_builder_add(
            &builder,
            "(siiii)",
            preview.window_id.c_str(),
            preview.x,
            preview.y,
            preview.width,
            preview.height);
        has_preview = true;
    }

    if (!has_preview)
    {
        if (callback)
            callback(false);
        return;
    }

    m_gnome_live_previews_requested = true;

    GAsyncReadyCallback ready = nullptr;
    gpointer completion_data = nullptr;
    if (callback)
    {
        auto completion =
            std::make_unique<LivePreviewsCompletion>();
        completion->state = m_state;
        completion->callback = std::move(callback);
        ready = complete_gnome_live_previews;
        completion_data = completion.release();
    }

    g_dbus_connection_call(
        m_state->connection,
        GNOME_THUMBNAIL_SERVICE,
        GNOME_THUMBNAIL_PATH,
        GNOME_THUMBNAIL_INTERFACE,
        "ShowLivePreviews",
        g_variant_new("(a(siiii))", &builder),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        nullptr,
        ready,
        completion_data);
}

void DockWindowThumbnailProvider::
    forward_gnome_preview_primary_click(
        const WindowId &window_id,
        double normalized_x,
        double normalized_y)
{
    if (!supports_gnome_live_previews() ||
        window_id.empty())
    {
        return;
    }

    g_dbus_connection_call(
        m_state->connection,
        GNOME_THUMBNAIL_SERVICE,
        GNOME_THUMBNAIL_PATH,
        GNOME_THUMBNAIL_INTERFACE,
        "ForwardPreviewPrimaryClick",
        g_variant_new(
            "(sdd)",
            window_id.c_str(),
            std::clamp(normalized_x, 0.0, 1.0),
            std::clamp(normalized_y, 0.0, 1.0)),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        nullptr,
        nullptr,
        nullptr);
}

void DockWindowThumbnailProvider::
    hide_gnome_live_previews()
{
    if (!m_gnome_live_previews_requested)
        return;

    m_gnome_live_previews_requested = false;

    if (!supports_gnome_live_previews())
        return;

    g_dbus_connection_call(
        m_state->connection,
        GNOME_THUMBNAIL_SERVICE,
        GNOME_THUMBNAIL_PATH,
        GNOME_THUMBNAIL_INTERFACE,
        "HideLivePreviews",
        nullptr,
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        nullptr,
        nullptr,
        nullptr);
}

void DockWindowThumbnailProvider::request(
    const WindowId &window_id,
    int target_width,
    int target_height,
    Callback callback,
    double x11_oversample,
    bool x11_native_capture,
    bool x11_strict_composite)
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
        x11_oversample,
        x11_native_capture,
        x11_strict_composite,
        std::move(callback))
        .detach();
}
