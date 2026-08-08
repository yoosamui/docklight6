// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// kwin_integration_protocol.h
//
// Purpose:
// Declares the protocol version and stable D-Bus names shared by Docklight
// integrations.
//
// Responsibilities:
// - Centralize service, object, interface, and version identifiers.
// - Provide one compatibility contract to each producer and consumer.
//
// Dependencies and ownership:
// Constants are compile-time values and own no runtime resources.
//
// Design notes:
// Protocol changes must be coordinated with the KWin script and Plasma
// bridge.
//
// ------------------------------------------------------------

#pragma once

#include <cstdint>

namespace KWinIntegrationProtocol
{

constexpr std::uint32_t VERSION = 7; // Window-integration protocol version
constexpr std::uint32_t LEGACY_VERSION =
    6; // Previous version accepted during script upgrades

constexpr char SERVICE_NAME[] =
    "org.docklight6.WindowIntegration"; // Window-integration D-Bus service
constexpr char OBJECT_PATH[] =
    "/org/docklight6/WindowIntegration"; // Window-integration D-Bus object
constexpr char INTERFACE_NAME[] =
    "org.docklight6.WindowIntegration1"; // Window-integration D-Bus interface

}
