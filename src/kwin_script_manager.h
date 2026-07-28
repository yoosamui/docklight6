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
