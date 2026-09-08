// ------------------------------------------------------------
// Docklight 6.0
//
// Exercises the actual private Hyprland pixel converter without a compositor.
// Reproduces the buffer/dimension mismatch from the September 8 resize crash
// and checks valid conversion, alpha handling, and rejected buffer bounds.
// ------------------------------------------------------------

#include "../preview/hyprland_thumbnail_capture.cpp"

#include <cassert>

int main()
{
    // The crash allocated 874 x 945 but decoded updated 887 x 959 dimensions.
    std::vector<uint32_t> pixels(874 * 945, 0x12345678U);
    HyprlandThumbnail thumbnail;
    thumbnail.width = 887;
    thumbnail.height = 959;
    assert(!convert_to_rgba(thumbnail, pixels.data(), pixels.size() * 4,
                            WL_SHM_FORMAT_XRGB8888));
    assert(thumbnail.rgba.empty());

    thumbnail.width = 874;
    thumbnail.height = 945;
    assert(convert_to_rgba(thumbnail, pixels.data(), pixels.size() * 4,
                           WL_SHM_FORMAT_XRGB8888));
    assert(thumbnail.rgba.size() == pixels.size() * 4);
    assert(thumbnail.rgba[0] == 0x34);
    assert(thumbnail.rgba[1] == 0x56);
    assert(thumbnail.rgba[2] == 0x78);
    assert(thumbnail.rgba.back() == 255);
    assert(!convert_to_rgba(thumbnail, pixels.data(), pixels.size() * 4 - 1,
                            WL_SHM_FORMAT_XRGB8888));

    thumbnail.width = 1;
    thumbnail.height = 1;
    assert(convert_to_rgba(thumbnail, pixels.data(), 4,
                           WL_SHM_FORMAT_ABGR8888));
    assert((thumbnail.rgba == std::vector<unsigned char>{0x78, 0x56, 0x34, 0x12}));
    assert(!convert_to_rgba(thumbnail, nullptr, 4, WL_SHM_FORMAT_XRGB8888));
    thumbnail.height = 0;
    assert(!convert_to_rgba(thumbnail, pixels.data(), 4, WL_SHM_FORMAT_XRGB8888));
    thumbnail.height = -1;
    assert(!convert_to_rgba(thumbnail, pixels.data(), 4, WL_SHM_FORMAT_XRGB8888));
    thumbnail.width = std::numeric_limits<int>::max();
    thumbnail.height = std::numeric_limits<int>::max();
    assert(!convert_to_rgba(thumbnail, pixels.data(), 4, WL_SHM_FORMAT_XRGB8888));
}
