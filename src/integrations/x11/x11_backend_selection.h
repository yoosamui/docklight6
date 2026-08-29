// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// x11_backend_selection.h
//
// Purpose:
// Declares pure X11 backend and desktop-file identity classification.
//
// Responsibilities:
// - Classify supported X11 window managers into backend kinds.
// - Provide stable diagnostic names for backend kinds.
// - Resolve known X11 application identities requiring caption metadata.
//
// Dependencies and ownership:
// Functions accept and return owned or borrowed string values and retain no
// window-manager, display, or application resources.
//
// Design notes:
// Pure classification keeps startup decisions deterministic and independently
// testable without opening an X display.
//
// ------------------------------------------------------------

#pragma once

#include <string>

enum class X11BackendKind
{
    kwin,
    marco,
    muffin,
    mutter,
    openbox,
    xfwm4,
    ewmh_fallback
};

X11BackendKind select_x11_backend_kind(
    const std::string &window_manager_name,
    const std::string &desktop_name);

const char *x11_backend_kind_name(
    X11BackendKind kind);

std::string resolve_x11_desktop_file_name(
    const std::string &class_group_name,
    const std::string &window_caption);
