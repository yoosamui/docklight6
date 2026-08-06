// Verifies stateless icon transformations without constructing GTK widgets.

#include "dock_icon_renderer.h"
#include "layout/dock_layout_metrics.h"

#include <gdkmm/wrap_init.h>
#include <glibmm/init.h>

#include <cassert>

int main()
{
    Glib::init();
    Gdk::wrap_init();

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

    assert(zoom_frames.size() == 9);
    assert(blur_frames.size() == 9);
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
