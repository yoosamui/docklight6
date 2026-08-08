// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// kwin_protocol_codec.cpp
//
// Implementation overview:
// Implements typed encoding, decoding, and validation for the KWin D-Bus
// protocol.
//
// Important implementation decisions:
// - Integer parsing rejects partial and out-of-range values.
// - Array serialization preserves delimiters through explicit escaping.
// - Window payloads are validated before reaching backend state.
//
// ------------------------------------------------------------
#include "kwin_protocol_codec.h"

#include <glib.h>
#include <charconv>
#include <cstring>
#include <sstream>
#include <system_error>

namespace
{
template <typename Integer>
bool parse_integer(
    const char *text,
    Integer &value)
{
    if (!text || text[0] == '\0')
        return false;

    const auto end =
        text + std::strlen(text);

    const auto result =
        std::from_chars(
            text,
            end,
            value);

    return result.ec == std::errc{} &&
           result.ptr == end;
}

bool parse_boolean(
    const char *text,
    bool &value)
{
    if (std::strcmp(text, "0") == 0)
    {
        value = false;
        return true;
    }

    if (std::strcmp(text, "1") == 0)
    {
        value = true;
        return true;
    }

    return false;
}

bool decode_string_array(
    const char *encoded_values,
    std::vector<std::string> &values)
{
    values.clear();

    if (!encoded_values ||
        encoded_values[0] == '\0')
    {
        return true;
    }

    auto parts =
        g_strsplit(
            encoded_values,
            ",",
            -1);

    for (int index = 0;
         parts[index];
         ++index)
    {
        auto decoded =
            g_uri_unescape_string(
                parts[index],
                nullptr);

        if (!decoded)
        {
            g_strfreev(parts);
            values.clear();
            return false;
        }

        values.emplace_back(decoded);
        g_free(decoded);
    }

    g_strfreev(parts);

    return true;
}

std::string encode_values(
    const std::vector<std::string>
        &values)
{
    std::ostringstream encoded;

    for (auto value = values.begin();
         value != values.end();
         ++value)
    {
        if (value != values.begin())
            encoded << ',';

        auto escaped =
            g_uri_escape_string(
                value->c_str(),
                nullptr,
                true);

        encoded << escaped;
        g_free(escaped);
    }

    return encoded.str();
}

bool parse_desktop_numbers(
    const char *encoded_values,
    std::vector<unsigned int> &numbers)
{
    std::vector<std::string> values;

    if (!decode_string_array(
            encoded_values,
            values))
    {
        return false;
    }

    numbers.clear();
    numbers.reserve(values.size());

    for (const auto &value : values)
    {
        unsigned int number = 0;

        if (!parse_integer(
                value.c_str(),
                number) ||
            number == 0)
        {
            numbers.clear();
            return false;
        }

        numbers.push_back(number);
    }

    return true;
}

std::optional<WindowId> decode_optional_window_id(
    const char *window_id)
{
    if (!window_id ||
        window_id[0] == '\0')
    {
        return std::nullopt;
    }

    return WindowId{window_id};
}

bool decode_window(
    GVariant *parameters,
    std::uint64_t &revision,
    ManagedWindow &window)
{
    const char *revision_text = nullptr;
    const char *window_payload = nullptr;

    g_variant_get(
        parameters,
        "(&s&s)",
        &revision_text,
        &window_payload);

    std::vector<std::string> fields;

    if (!decode_string_array(
            window_payload,
            fields) ||
        fields.size() != 16)
    {
        return false;
    }

    if (!parse_integer(
            revision_text,
            revision) ||
        !parse_integer(
            fields[4].c_str(),
            window.process_id) ||
        !parse_boolean(
            fields[5].c_str(),
            window.minimized) ||
        !parse_boolean(
            fields[6].c_str(),
            window.maximized) ||
        !parse_boolean(
            fields[7].c_str(),
            window.skip_taskbar) ||
        !parse_integer(
            fields[8].c_str(),
            window.frame_geometry.x) ||
        !parse_integer(
            fields[9].c_str(),
            window.frame_geometry.y) ||
        !parse_integer(
            fields[10].c_str(),
            window.frame_geometry.width) ||
        !parse_integer(
            fields[11].c_str(),
            window.frame_geometry.height) ||
        !decode_string_array(
            fields[12].c_str(),
            window.activity_ids) ||
        !decode_string_array(
            fields[13].c_str(),
            window.desktop_ids) ||
        !parse_desktop_numbers(
            fields[14].c_str(),
            window.desktop_numbers) ||
        !parse_boolean(
            fields[15].c_str(),
            window.on_current_desktop))
    {
        return false;
    }

    window.id = fields[0];
    window.desktop_file_name =
        fields[1];
    window.caption = fields[2];
    window.icon_name = fields[3];

    return !window.id.empty();
}

}

bool KWinProtocolCodec::parse_protocol_version(
    const char *text,
    std::uint32_t &version)
{
    return parse_integer(text, version);
}

bool KWinProtocolCodec::parse_revision(
    const char *text,
    std::uint64_t &revision)
{
    return parse_integer(text, revision);
}

bool KWinProtocolCodec::parse_string_array(
    const char *encoded_values,
    std::vector<std::string> &values)
{
    return decode_string_array(encoded_values, values);
}

std::string KWinProtocolCodec::encode_string_array(
    const std::vector<std::string> &values)
{
    return encode_values(values);
}

std::optional<WindowId> KWinProtocolCodec::optional_window_id(
    const char *window_id)
{
    return decode_optional_window_id(window_id);
}

bool KWinProtocolCodec::parse_window(
    GVariant *parameters,
    std::uint64_t &revision,
    ManagedWindow &window)
{
    return decode_window(parameters, revision, window);
}
