cmake_minimum_required(VERSION 3.22)

project(
    docklight_geometry_bridge
    VERSION 0.1
    LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(
    Qt6 REQUIRED
    COMPONENTS Core DBus Qml)

add_library(
    docklightgeometrybridgeplugin
    MODULE
    geometry_bridge.cpp
    geometry_bridge.h
    geometry_bridge_plugin.cpp)

target_link_libraries(
    docklightgeometrybridgeplugin
    PRIVATE
    Qt6::Core
    Qt6::DBus
    Qt6::Qml)
