#pragma once

#include <cstdint>

namespace KWinIntegrationProtocol
{

constexpr std::uint32_t VERSION = 3;

constexpr char SERVICE_NAME[] =
    "org.docklight6.WindowIntegration";
constexpr char OBJECT_PATH[] =
    "/org/docklight6/WindowIntegration";
constexpr char INTERFACE_NAME[] =
    "org.docklight6.WindowIntegration1";

}
