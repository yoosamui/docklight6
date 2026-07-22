
#pragma once

class DockPlacement
{
public:
    enum class Location
    {
        Bottom,
        Top,
        Left,
        Right
    };

    enum class Alignment
    {
        Fill,
        Center,
        Start,
        End
    };

    DockPlacement() = default;

    void set_location(Location location);
    Location location() const;

    void set_alignment(Alignment alignment);
    Alignment alignment() const;

    bool is_horizontal() const;
    bool is_vertical() const;

private:
    Location m_location = Location::Bottom;
    Alignment m_alignment = Alignment::Center;
};
