#pragma once

#include <cstdint>

namespace KWinIntegrationProtocol
{

constexpr std::uint32_t VERSION = 5; // Window-integration protocol version

constexpr char SERVICE_NAME[] =
    "org.docklight6.WindowIntegration"; // Window-integration D-Bus service
constexpr char OBJECT_PATH[] =
    "/org/docklight6/WindowIntegration"; // Window-integration D-Bus object
constexpr char INTERFACE_NAME[] =
    "org.docklight6.WindowIntegration1"; // Window-integration D-Bus interface

}
