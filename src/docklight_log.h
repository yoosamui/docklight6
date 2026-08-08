// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// docklight_log.h
//
// Purpose:
// Declares Docklight logging initialization and the always-visible startup
// message helper.
//
// Responsibilities:
// - Install process-wide release filtering.
// - Emit formatted startup diagnostics.
// - Keep logging policy behind a small application interface.
//
// Dependencies and ownership:
// The logging subsystem owns only process-wide writer configuration and no
// caller data.
//
// Design notes:
// Initialization must occur before other subsystems emit diagnostics.
//
// ------------------------------------------------------------

#pragma once

namespace DocklightLog
{

// Installs the release log filter before any subsystem can emit diagnostics.
// Debug builds leave the default GLib writer unchanged.
void initialize();

// Writes an always-visible message belonging to the startup sequence.
void startup(
    const char *format,
    ...)
    __attribute__((format(printf, 1, 2)));

}
