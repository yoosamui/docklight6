#pragma once

#include "dock_layout_types.h"

#include <string>

class DockSettings
{
public:
    const std::string &monitor() const;
    void set_monitor(
        const std::string &identifier);

    int icon_size() const;
    void set_icon_size(int size);

    DockHoverEffect hover_effect() const;
    void set_hover_effect(
        DockHoverEffect effect);

    int minimum_bottom_workarea_inset() const;
    void set_minimum_bottom_workarea_inset(int inset);

private:
    std::string m_monitor = "primary";

    // Preserve the current launcher-icon request while making it configurable
    // instead of keeping it as a DockItem literal.
    int m_icon_size = 46; // 512 test value. default is 46px

    DockHoverEffect m_hover_effect =
        DockHoverEffect::standard;

    // Some Wayland compositors report the full output as the work area even
    // when a panel occludes the bottom edge. This is the minimum hidden
    // screen inset; the dock's visible edge margin is added separately.
    int m_minimum_bottom_workarea_inset = 36;
};
