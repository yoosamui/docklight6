// ------------------------------------------------------------
// Docklight 6.0
//
// Declares release log filtering and the always-visible startup log.
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
