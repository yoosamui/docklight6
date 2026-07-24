#include "dock_settings.h"

#include <algorithm>


DockSettings g_settings;


int DockSettings::icon_size() const
{
    return m_icon_size;
}

void DockSettings::set_icon_size(int size)
{
    m_icon_size = size;
}

int DockSettings::minimum_bottom_workarea_inset() const
{
    return m_minimum_bottom_workarea_inset;
}

void DockSettings::set_minimum_bottom_workarea_inset(
    int inset)
{
    m_minimum_bottom_workarea_inset =
        std::max(0, inset);
}
