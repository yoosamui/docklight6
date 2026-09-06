// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_icon_renderer_test.cpp
//
// Implementation overview:
// Verifies stateless icon transformations without constructing GTK widgets.
//
// Important implementation decisions:
// - GLib and GDK are initialized without a display-backed window.
// - Pixel and frame assertions cover highlight, zoom, blur, and magnification.
// - Alpha centroids detect subpixel wobble across magnified frame sizes.
// - Shared layout metrics define expected rendered dimensions.
//
// ------------------------------------------------------------

#include "rendering/dock_icon_renderer.h"
#include "layout/dock_layout_metrics.h"

#include <gdkmm/wrap_init.h>
#include <glibmm/init.h>

#include <cassert>
#include <cmath>

// A symmetric icon must retain its center while crossing every odd/even
// pixel size. Integer centering used to alternate by half a pixel and made
// slow magnification visibly shimmer.
static void check_magnified_center(int size)
{
    auto source = Gdk::Pixbuf::create(
        Gdk::COLORSPACE_RGB, true, 8, size, size);
    source->fill(0xffffffff);
    for (int step = 0; step <= 125; ++step)
    {
        const auto frame = DockIconRenderer::create_magnified(
            source, size, 1.0 + step / 100.0);
        double mass = 0.0;
        double moment_x = 0.0;
        double moment_y = 0.0;
        for (int y = 0; y < frame->get_height(); ++y)
        {
            for (int x = 0; x < frame->get_width(); ++x)
            {
                const auto alpha = frame->get_pixels()[
                    y * frame->get_rowstride() + x * 4 + 3];
                mass += alpha;
                moment_x += (x + 0.5) * alpha;
                moment_y += (y + 0.5) * alpha;
            }
        }
        assert(mass > 0.0);
        assert(std::abs(moment_x / mass -
                        frame->get_width() / 2.0) < 0.05);
        assert(std::abs(moment_y / mass -
                        frame->get_height() / 2.0) < 0.05);
    }
}

int main()
{
    Glib::init();
    Gdk::wrap_init();

    check_magnified_center(47);
    check_magnified_center(48);

    auto source = Gdk::Pixbuf::create(
        Gdk::COLORSPACE_RGB,
        true,
        8,
        16,
        16);
    source->fill(0x102030ff);

    const auto highlighted =
        DockIconRenderer::create_standard_hover(source);
    assert(highlighted);
    const auto *pixel = highlighted->get_pixels();
    assert(pixel[0] == 44);
    assert(pixel[1] == 64);
    assert(pixel[2] == 84);
    assert(pixel[3] == 255);

    constexpr int icon_size = 32;
    const int item_size =
        DockLayoutMetrics::item_size_for(icon_size);
    const auto zoom_frames =
        DockIconRenderer::create_zoom_frames(source, icon_size);
    const auto blur_frames =
        DockIconRenderer::create_blur_frames(source, icon_size);
    const auto magnified =
        DockIconRenderer::create_magnified(source, icon_size, 2.25);
    const auto magnified_at_base_scale =
        DockIconRenderer::create_magnified(source, icon_size, 1.0);

    assert(zoom_frames.size() == 9);
    assert(blur_frames.size() == 9);
    assert(magnified);
    assert(magnified_at_base_scale);
    assert(magnified->get_width() > item_size);
    assert(magnified->get_height() > item_size);
    assert(magnified->get_width() ==
           magnified_at_base_scale->get_width());
    assert(magnified->get_height() ==
           magnified_at_base_scale->get_height());
    for (const auto &frame : zoom_frames)
    {
        assert(frame->get_width() == item_size);
        assert(frame->get_height() == item_size);
    }
    for (const auto &frame : blur_frames)
    {
        assert(frame->get_width() == item_size);
        assert(frame->get_height() == item_size);
    }

    return 0;
}
