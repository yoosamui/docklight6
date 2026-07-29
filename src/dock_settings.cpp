#include "dock_settings.h"

#include <algorithm>

const std::string &DockSettings::monitor() const
{
    return m_monitor;
}

void DockSettings::set_monitor(
    const std::string &identifier)
{
    m_monitor =
        identifier.empty()
            ? "primary"
            : identifier;
}

int DockSettings::icon_size() const
{
    return m_icon_size;
}

void DockSettings::set_icon_size(int size)
{
    m_icon_size = size;
}

DockHoverEffect DockSettings::hover_effect() const
{
    return m_hover_effect;
}

void DockSettings::set_hover_effect(
    DockHoverEffect effect)
{
    m_hover_effect = effect;
}

DockIndicator DockSettings::indicator() const
{
    return m_indicator;
}

void DockSettings::set_indicator(
    DockIndicator indicator)
{
    m_indicator = indicator;
}

const std::string &
DockSettings::indicator_color() const
{
    return m_indicator_color;
}

void DockSettings::set_indicator_color(
    const std::string &color)
{
    m_indicator_color = color;
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
