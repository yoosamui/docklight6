#pragma once

struct WindowIconGeometry
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool operator==(
        const WindowIconGeometry &other) const
    {
        return x == other.x &&
               y == other.y &&
               width == other.width &&
               height == other.height;
    }

    bool operator!=(
        const WindowIconGeometry &other) const
    {
        return !(*this == other);
    }
};
