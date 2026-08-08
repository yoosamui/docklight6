// ------------------------------------------------------------
// Docklight 6.0
//
// ------------------------------------------------------------
//
// File:
// kwin_script_manager.h
//
// Purpose:
// Declares installation-path discovery and restart support for Docklight's
// KWin script.
//
// Responsibilities:
// - Locate the installed integration script.
// - Request unload and load operations through KWin scripting.
// - Report whether the integration was restarted successfully.
//
// Dependencies and ownership:
// The manager owns no persistent state; KWin owns loaded script instances.
//
// Design notes:
// Script lifecycle support remains separate from window backend
// synchronization.
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
