// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// docklight_log.cpp
//
// Implementation overview:
// Implements release log filtering and the always-visible startup log
// channel.
//
// Important implementation decisions:
// - Release builds suppress nonessential GLib diagnostics.
// - Startup messages bypass the general filter.
// - Debug builds retain the default GLib writer.
//
// ------------------------------------------------------------

#include "docklight_log.h"

#include <glib.h>

#include <cstdarg>
#include <cstring>

namespace
{

constexpr char STARTUP_LOG_DOMAIN[] =
    "docklight-startup";

#ifndef DEBUG
GLogWriterOutput release_log_writer(
    GLogLevelFlags log_level,
    const GLogField *fields,
    gsize field_count,
    gpointer)
{
    for (gsize index = 0;
         index < field_count;
         ++index)
    {
        const auto &field = fields[index];

        if (std::strcmp(
                field.key,
                "GLIB_DOMAIN") != 0 ||
            !field.value)
        {
            continue;
        }

        if (std::strcmp(
                static_cast<const char *>(
                    field.value),
                STARTUP_LOG_DOMAIN) == 0)
        {
            return g_log_writer_default(
                log_level,
                fields,
                field_count,
                nullptr);
        }
    }

    return G_LOG_WRITER_HANDLED;
}
#endif

}

void DocklightLog::initialize()
{
#ifndef DEBUG
    g_log_set_writer_func(
        release_log_writer,
        nullptr,
        nullptr);
#endif
}

void DocklightLog::startup(
    const char *format,
    ...)
{
    std::va_list arguments;
    va_start(arguments, format);

    g_logv(
        STARTUP_LOG_DOMAIN,
        G_LOG_LEVEL_MESSAGE,
        format,
        arguments);

    va_end(arguments);
}
