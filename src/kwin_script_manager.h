// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// Declares installation-path discovery and restart of Docklight's KWin
// window-integration script.
//
// The manager owns no persistent state; KWin owns the loaded script
// after the D-Bus scripting operations complete.
//
// ------------------------------------------------------------

#pragma once

#include <string>

class KWinScriptManager
{
public:
    bool restart();

private:
    static std::string
    installed_script_path();
};
