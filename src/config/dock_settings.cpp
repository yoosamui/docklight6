// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// Implements the typed DockSettings accessors and local value
// normalization used after configuration parsing.
//
// This file does not read configuration files or update GTK widgets;
// it only maintains settings owned by a DockSettings instance.
//
// ------------------------------------------------------------

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

int DockSettings::preview_card_height() const
{
    return m_preview_card_height;
}

void DockSettings::set_preview_card_height(
    int height)
{
    m_preview_card_height =
        height == 0 ||
                (height >= 64 && height <= 512)
            ? height
            : 0;
}

int DockSettings::preview_show_delay() const
{
    return m_preview_show_delay;
}

void DockSettings::set_preview_show_delay(
    int delay_ms)
{
    m_preview_show_delay =
        std::clamp(delay_ms, 0, 5000);
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

bool DockSettings::home_icon_enabled() const
{
    return m_home_icon_enabled;
}

void DockSettings::set_home_icon_enabled(
    bool enabled)
{
    m_home_icon_enabled = enabled;
}

const std::string &
DockSettings::home_icon_path() const
{
    return m_home_icon_path;
}

void DockSettings::set_home_icon_path(
    const std::string &path)
{
    m_home_icon_path = path;
}

bool DockSettings::display_tooltips() const
{
    return m_display_tooltips;
}

void DockSettings::set_display_tooltips(
    bool enabled)
{
    m_display_tooltips = enabled;
}

bool DockSettings::display_preview() const
{
    return m_display_preview;
}

void DockSettings::set_display_preview(
    bool enabled)
{
    m_display_preview = enabled;
}

bool DockSettings::close_preview_after_activation() const
{
    return m_close_preview_after_activation;
}

void DockSettings::set_close_preview_after_activation(
    bool enabled)
{
    m_close_preview_after_activation = enabled;
}

bool DockSettings::manage_all_workspaces() const
{
    return m_manage_all_workspaces;
}

void DockSettings::set_manage_all_workspaces(
    bool enabled)
{
    m_manage_all_workspaces = enabled;
}
