// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// kwin_protocol_codec.h
//
// Purpose:
// Declares typed conversion and validation helpers for KWin protocol
// payloads.
//
// Responsibilities:
// - Parse protocol versions, revisions, arrays, and window values.
// - Encode outbound values using the shared wire representation.
// - Reject malformed transport data at the integration boundary.
//
// Dependencies and ownership:
// Functions operate on caller-owned values and return owned decoded data;
// the codec holds no state.
//
// Design notes:
// Transport parsing is isolated from KWinWindowBackend state management.
//
// ------------------------------------------------------------
#pragma once

#include "windowing/managed_window.h"
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
