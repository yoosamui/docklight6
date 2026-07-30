#pragma once

#include "dock_layout_types.h"

#include <string>

class DockSettings
{
public:
    void set_monitor(
        const std::string &identifier);
    void set_icon_size(int size);
    void set_hover_effect(
        DockHoverEffect effect);
    void set_indicator(
        DockIndicator indicator);
    void set_indicator_color(
        const std::string &color);
    void set_minimum_bottom_workarea_inset(int inset);
    void set_home_icon_enabled(bool enabled);
    void set_home_icon_path(
        const std::string &path);
    void set_display_tooltips(bool enabled);
    void set_manage_all_workspaces(bool enabled);

    const std::string &monitor() const;

    DockHoverEffect hover_effect() const;
    DockIndicator indicator() const;
    const std::string &
    indicator_color() const;

    int icon_size() const;
    int minimum_bottom_workarea_inset() const;
    bool home_icon_enabled() const;
    const std::string &home_icon_path() const;
    bool display_tooltips() const;
    bool manage_all_workspaces() const;

private:
    std::string m_monitor = "primary";

    // Preserve the current launcher-icon request while making it configurable
    // instead of keeping it as a DockItem literal.
    int m_icon_size = 46; // 512 test value. default is 46px

    // Some Wayland compositors report the full output as the work area even
    // when a panel occludes the bottom edge. This is the minimum hidden
    // screen inset; the dock's visible edge margin is added separately.
    int m_minimum_bottom_workarea_inset = 36;

    DockHoverEffect m_hover_effect =
        DockHoverEffect::standard;

    DockIndicator m_indicator =
        DockIndicator::lines;

    std::string m_indicator_color =
        "#69aaff";

    bool m_home_icon_enabled = true;
    std::string m_home_icon_path;
    bool m_display_tooltips = true;
    bool m_manage_all_workspaces = true;
};
