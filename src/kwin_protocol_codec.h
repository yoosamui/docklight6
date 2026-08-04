// Typed encoding and validation for the KWin D-Bus protocol.
#pragma once

#include "managed_window.h"
#include <gio/gio.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace KWinProtocolCodec
{
bool parse_protocol_version(const char *text, std::uint32_t &version);
bool parse_revision(const char *text, std::uint64_t &revision);
bool parse_string_array(
    const char *encoded_values,
    std::vector<std::string> &values);
std::string encode_string_array(
    const std::vector<std::string> &values);
std::optional<WindowId> optional_window_id(const char *window_id);
bool parse_window(
    GVariant *parameters,
    std::uint64_t &revision,
    ManagedWindow &window);
}
