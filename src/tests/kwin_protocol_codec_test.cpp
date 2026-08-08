// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// kwin_protocol_codec_test.cpp
//
// Implementation overview:
// Verifies KWin protocol encoding, decoding, and validation in isolation.
//
// Important implementation decisions:
// - Valid and malformed scalar values exercise strict parsing.
// - Array round trips cover escaping and delimiter preservation.
// - Window payload tests enforce required fields and value ranges.
//
// ------------------------------------------------------------

#include "integrations/kwin/kwin_protocol_codec.h"

#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace
{
void verifies_scalar_and_array_parsing()
{
    std::uint32_t version = 0;
    std::uint64_t revision = 0;

    assert(KWinProtocolCodec::parse_protocol_version("6", version));
    assert(version == 6);
    assert(!KWinProtocolCodec::parse_protocol_version("6x", version));
    assert(KWinProtocolCodec::parse_revision("184", revision));
    assert(revision == 184);
    assert(!KWinProtocolCodec::parse_revision("", revision));

    const std::vector<std::string> expected{
        "plain",
        "contains,comma",
        "space and / slash"};
    const auto encoded =
        KWinProtocolCodec::encode_string_array(expected);
    std::vector<std::string> decoded;
    assert(KWinProtocolCodec::parse_string_array(
        encoded.c_str(),
        decoded));
    assert(decoded == expected);

    assert(!KWinProtocolCodec::optional_window_id("").has_value());
    assert(KWinProtocolCodec::optional_window_id("window-1") ==
           std::optional<WindowId>{"window-1"});
}

void verifies_window_payload_validation()
{
    const std::vector<std::string> fields{
        "window-1",
        "org.example.App.desktop",
        "Example",
        "example-icon",
        "1234",
        "0",
        "1",
        "0",
        "10",
        "20",
        "800",
        "600",
        KWinProtocolCodec::encode_string_array({"activity-1"}),
        KWinProtocolCodec::encode_string_array({"desktop-uuid"}),
        KWinProtocolCodec::encode_string_array({"2"}),
        "1"};
    const auto payload =
        KWinProtocolCodec::encode_string_array(fields);
    auto *parameters = g_variant_ref_sink(
        g_variant_new("(ss)", "42", payload.c_str()));

    std::uint64_t revision = 0;
    ManagedWindow window;
    assert(KWinProtocolCodec::parse_window(
        parameters,
        revision,
        window));
    assert(revision == 42);
    assert(window.id == "window-1");
    assert(window.process_id == 1234);
    assert(window.maximized);
    assert(window.desktop_numbers ==
           std::vector<unsigned int>{2});
    g_variant_unref(parameters);

    parameters = g_variant_ref_sink(
        g_variant_new("(ss)", "42", "too,few,fields"));
    assert(!KWinProtocolCodec::parse_window(
        parameters,
        revision,
        window));
    g_variant_unref(parameters);
}
}

int main()
{
    verifies_scalar_and_array_parsing();
    verifies_window_payload_validation();
    return 0;
}
