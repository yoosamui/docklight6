// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// Defines the version and stable D-Bus names shared by Docklight, its
// KWin script, and the Plasma geometry bridge.
//
// These protocol constants own no resources. Compatibility changes
// must be coordinated with every producer and consumer.
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
