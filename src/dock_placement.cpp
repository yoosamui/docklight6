#include "dock_placement.h"

void DockPlacement::set_location(Location location)
{
    m_location = location;
}

DockPlacement::Location DockPlacement::location() const
{
    return m_location;
}

void DockPlacement::set_alignment(Alignment alignment)
{
    m_alignment = alignment;
}

DockPlacement::Alignment DockPlacement::alignment() const
{
    return m_alignment;
}

bool DockPlacement::is_horizontal() const
{
    return
        m_location == Location::Bottom ||
        m_location == Location::Top;
}

bool DockPlacement::is_vertical() const
{
    return !is_horizontal();
}