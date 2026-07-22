#pragma once

class DockSettings
{
public:
    int icon_size() const;

    void set_icon_size(int size);

private:
    // Preserve the current launcher-icon request while making it configurable
    // through g_settings instead of a DockItem literal.
    int m_icon_size = 46;
};


extern DockSettings g_settings;
