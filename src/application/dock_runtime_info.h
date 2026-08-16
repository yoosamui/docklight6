// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// dock_runtime_info.h
//
// Purpose:
// Carries startup diagnostics to user-facing application surfaces.
//
// Responsibilities:
// - Preserve the presentation and configuration choices made at startup.
// - Preserve the detected desktop environment and selected window backend.
//
// Dependencies and ownership:
// This value type owns its strings and has no runtime resource dependencies.
//
// Design notes:
// Detection remains in WindowSystemController; this type only transports the
// resulting snapshot to UI components such as the About dialog.
//
// ------------------------------------------------------------

#pragma once

#include <string>

struct DockRuntimeInfo
{
    std::string presentation_mode;
    std::string configuration_path;
    std::string desktop;
    std::string window_manager;
    std::string compositor;
    std::string backend;
};
