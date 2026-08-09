// ------------------------------------------------------------
// Docklight 6.0
//
// Pure X11 backend classification used by startup and regression tests.
// ------------------------------------------------------------

#pragma once

#include <string>

enum class X11BackendKind
{
    muffin,
    mutter,
    xfwm4,
    ewmh_fallback
};

X11BackendKind select_x11_backend_kind(
    const std::string &window_manager_name,
    const std::string &desktop_name);

const char *x11_backend_kind_name(
    X11BackendKind kind);
