// ------------------------------------------------------------
// Docklight 6.0
//
// Implements stateless dock icon transformations.
// ------------------------------------------------------------

#include "dock_icon_renderer.h"
#include "layout/dock_layout_metrics.h"

#include <algorithm>

namespace
{
constexpr int FRAME_COUNT = 9;
constexpr int BLUR_OUTER_RED = 105;
constexpr int BLUR_OUTER_GREEN = 170;
constexpr int BLUR_OUTER_BLUE = 255;
constexpr int BLUR_INNER_RED = 235;
constexpr int BLUR_INNER_GREEN = 245;
constexpr int BLUR_INNER_BLUE = 255;
constexpr int BLUR_OUTER_MAX_ALPHA = 225;
constexpr int BLUR_INNER_MAX_ALPHA = 190;

Glib::RefPtr<Gdk::Pixbuf> create_transparent_pixbuf(
    int width,
    int height)
{
    auto pixbuf = Gdk::Pixbuf::create(
        Gdk::COLORSPACE_RGB,
        true,
        8,
        std::max(1, width),
        std::max(1, height));
    pixbuf->fill(0x00000000);
    return pixbuf;
}

std::vector<float> box_blur_alpha(
    const std::vector<float> &source,
    int width,
    int height,
    int radius)
{
    if (radius <= 0)
        return source;

    const int diameter = radius * 2 + 1;
    std::vector<float> horizontal(source.size(), 0.0F);
    std::vector<float> result(source.size(), 0.0F);

    for (int y = 0; y < height; ++y)
    {
        float sum = 0.0F;
        for (int offset = -radius; offset <= radius; ++offset)
        {
            if (offset >= 0 && offset < width)
                sum += source[static_cast<std::size_t>(y * width + offset)];
        }

        for (int x = 0; x < width; ++x)
        {
            horizontal[static_cast<std::size_t>(y * width + x)] =
                sum / diameter;

            const int remove_x = x - radius;
            const int add_x = x + radius + 1;
            if (remove_x >= 0)
                sum -= source[static_cast<std::size_t>(y * width + remove_x)];
            if (add_x < width)
                sum += source[static_cast<std::size_t>(y * width + add_x)];
        }
    }

    for (int x = 0; x < width; ++x)
    {
        float sum = 0.0F;
        for (int offset = -radius; offset <= radius; ++offset)
        {
            if (offset >= 0 && offset < height)
                sum += horizontal[static_cast<std::size_t>(offset * width + x)];
        }

        for (int y = 0; y < height; ++y)
        {
            result[static_cast<std::size_t>(y * width + x)] = sum / diameter;

            const int remove_y = y - radius;
            const int add_y = y + radius + 1;
            if (remove_y >= 0)
                sum -= horizontal[static_cast<std::size_t>(remove_y * width + x)];
            if (add_y < height)
                sum += horizontal[static_cast<std::size_t>(add_y * width + x)];
        }
    }

    return result;
}

std::vector<float> blur_alpha(
    std::vector<float> alpha,
    int width,
    int height,
    int radius)
{
    for (int pass = 0; pass < 3; ++pass)
        alpha = box_blur_alpha(alpha, width, height, radius);
    return alpha;
}

Glib::RefPtr<Gdk::Pixbuf> create_blur_pixbuf(
    const std::vector<float> &alpha,
    int size,
    int red,
    int green,
    int blue)
{
    auto blur = create_transparent_pixbuf(size, size);
    auto *pixels = blur->get_pixels();
    const int rowstride = blur->get_rowstride();

    for (int y = 0; y < size; ++y)
    {
        auto *row = pixels + y * rowstride;
        for (int x = 0; x < size; ++x)
        {
            auto *pixel = row + x * 4;
            pixel[0] = static_cast<guchar>(red);
            pixel[1] = static_cast<guchar>(green);
            pixel[2] = static_cast<guchar>(blue);
            pixel[3] = static_cast<guchar>(std::clamp(
                alpha[static_cast<std::size_t>(y * size + x)],
                0.0F,
                255.0F));
        }
    }

    return blur;
}
}

Glib::RefPtr<Gdk::Pixbuf> DockIconRenderer::create_standard_hover(
    const Glib::RefPtr<Gdk::Pixbuf> &source)
{
    if (!source)
        return {};

    auto highlighted = source->copy();
    auto *pixels = highlighted->get_pixels();
    const int width = highlighted->get_width();
    const int height = highlighted->get_height();
    const int rowstride = highlighted->get_rowstride();
    const int channels = highlighted->get_n_channels();

    for (int y = 0; y < height; ++y)
    {
        auto *row = pixels + y * rowstride;
        for (int x = 0; x < width; ++x)
        {
            auto *pixel = row + x * channels;
            for (int channel = 0; channel < 3; ++channel)
            {
                pixel[channel] = static_cast<guchar>(std::min(
                    255,
                    static_cast<int>(pixel[channel]) * 5 / 4 + 24));
            }
        }
    }

    return highlighted;
}

std::vector<Glib::RefPtr<Gdk::Pixbuf>>
DockIconRenderer::create_zoom_frames(
    const Glib::RefPtr<Gdk::Pixbuf> &source,
    int icon_size)
{
    std::vector<Glib::RefPtr<Gdk::Pixbuf>> frames;
    if (!source)
        return frames;

    const int item_size = DockLayoutMetrics::item_size_for(icon_size);
    const int source_width = source->get_width();
    const int source_height = source->get_height();
    const int source_extent = std::max(source_width, source_height);
    const int zoom_percent = std::min(
        DockLayoutMetrics::HOVER_ZOOM_PERCENT,
        item_size * 100 / std::max(1, source_extent));
    const int target_width = std::max(
        source_width,
        source_width * zoom_percent / 100);
    const int target_height = std::max(
        source_height,
        source_height * zoom_percent / 100);

    frames.reserve(FRAME_COUNT);
    for (int frame_index = 0; frame_index < FRAME_COUNT; ++frame_index)
    {
        const int width = source_width +
            (target_width - source_width) * frame_index / (FRAME_COUNT - 1);
        const int height = source_height +
            (target_height - source_height) * frame_index / (FRAME_COUNT - 1);
        const int icon_x = (item_size - width) / 2;
        const int icon_y = (item_size - height) / 2;
        auto frame = create_transparent_pixbuf(item_size, item_size);

        source->composite(
            frame,
            icon_x,
            icon_y,
            width,
            height,
            icon_x,
            icon_y,
            static_cast<double>(width) / source_width,
            static_cast<double>(height) / source_height,
            Gdk::INTERP_BILINEAR,
            255);
        frames.push_back(frame);
    }

    return frames;
}

std::vector<Glib::RefPtr<Gdk::Pixbuf>>
DockIconRenderer::create_blur_frames(
    const Glib::RefPtr<Gdk::Pixbuf> &source,
    int icon_size)
{
    std::vector<Glib::RefPtr<Gdk::Pixbuf>> frames;
    if (!source)
        return frames;

    const int item_size = DockLayoutMetrics::item_size_for(icon_size);
    const int icon_width = source->get_width();
    const int icon_height = source->get_height();
    const int icon_x = (item_size - icon_width) / 2;
    const int icon_y = (item_size - icon_height) / 2;
    std::vector<float> icon_alpha(
        static_cast<std::size_t>(item_size * item_size),
        0.0F);
    const auto *icon_pixels = source->get_pixels();
    const int icon_rowstride = source->get_rowstride();
    const int icon_channels = source->get_n_channels();
    const bool icon_has_alpha = source->get_has_alpha();

    for (int y = 0; y < icon_height; ++y)
    {
        const auto *row = icon_pixels + y * icon_rowstride;
        for (int x = 0; x < icon_width; ++x)
        {
            const auto *pixel = row + x * icon_channels;
            icon_alpha[static_cast<std::size_t>(
                (icon_y + y) * item_size + icon_x + x)] =
                icon_has_alpha ? pixel[icon_channels - 1] : 255.0F;
        }
    }

    const auto outer_alpha = blur_alpha(
        icon_alpha,
        item_size,
        item_size,
        std::max(2, item_size / 24));
    const auto inner_alpha = blur_alpha(
        icon_alpha,
        item_size,
        item_size,
        std::max(1, item_size / 52));
    const auto outer_blur = create_blur_pixbuf(
        outer_alpha,
        item_size,
        BLUR_OUTER_RED,
        BLUR_OUTER_GREEN,
        BLUR_OUTER_BLUE);
    const auto inner_blur = create_blur_pixbuf(
        inner_alpha,
        item_size,
        BLUR_INNER_RED,
        BLUR_INNER_GREEN,
        BLUR_INNER_BLUE);

    frames.reserve(FRAME_COUNT);
    for (int frame_index = 0; frame_index < FRAME_COUNT; ++frame_index)
    {
        auto frame = create_transparent_pixbuf(item_size, item_size);
        const int outer_opacity =
            BLUR_OUTER_MAX_ALPHA * frame_index / (FRAME_COUNT - 1);
        const int inner_opacity =
            BLUR_INNER_MAX_ALPHA * frame_index / (FRAME_COUNT - 1);

        if (outer_opacity > 0)
        {
            outer_blur->composite(
                frame, 0, 0, item_size, item_size, 0, 0,
                1.0, 1.0, Gdk::INTERP_BILINEAR, outer_opacity);
            inner_blur->composite(
                frame, 0, 0, item_size, item_size, 0, 0,
                1.0, 1.0, Gdk::INTERP_BILINEAR, inner_opacity);
        }

        source->composite(
            frame,
            icon_x,
            icon_y,
            icon_width,
            icon_height,
            icon_x,
            icon_y,
            1.0,
            1.0,
            Gdk::INTERP_NEAREST,
            255);
        frames.push_back(frame);
    }

    return frames;
}
