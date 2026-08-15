#include "openbox_window_backend.h"

OpenboxWindowBackend::OpenboxWindowBackend()
    : EwmhWindowBackend("Openbox/X11")
{
}

WindowBackendCapabilities
OpenboxWindowBackend::capabilities() const
{
    auto result =
        EwmhWindowBackend::capabilities();
    result.thumbnail_policy =
        WindowThumbnailPolicy::
            redirect_and_cache_mapped_windows;
    result.thumbnails_require_compositor = true;
    return result;
}
