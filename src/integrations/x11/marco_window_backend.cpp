#include "marco_window_backend.h"

MarcoWindowBackend::MarcoWindowBackend()
    : EwmhWindowBackend("Marco/Metacity X11")
{
}

WindowBackendCapabilities
MarcoWindowBackend::capabilities() const
{
    auto result =
        EwmhWindowBackend::capabilities();
    result.thumbnail_policy =
        WindowThumbnailPolicy::cache_mapped_windows;
    return result;
}
