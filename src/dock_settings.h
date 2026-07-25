#pragma once

class DockSettings
{
public:
    int icon_size() const;
    void set_icon_size(int size);

    int minimum_bottom_workarea_inset() const;
    void set_minimum_bottom_workarea_inset(int inset);

private:
    // Preserve the current launcher-icon request while making it configurable
    // instead of keeping it as a DockItem literal.
    int m_icon_size = 46; // 512 test value. default is 46px

    // Some Wayland compositors report the full output as the work area even
    // when a panel occludes the bottom edge. This is the minimum hidden
    // screen inset; the dock's visible edge margin is added separately.
    int m_minimum_bottom_workarea_inset = 36;
};
