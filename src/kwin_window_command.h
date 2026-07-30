#pragma once

#include "managed_window.h"

#include <functional>
#include <vector>

enum class KWinWindowCommandType
{
    ACTIVATE,
    RAISE,
    CLOSE,
    SET_MINIMIZED,
    SET_MAXIMIZED,
    PRESENT,
    HIDE
};

struct KWinWindowCommand
{
    WindowId window_id;

    KWinWindowCommandType type =
        KWinWindowCommandType::ACTIVATE;

    bool state = false;

    std::vector<WindowId> window_ids;
};

using KWinWindowCommandHandler =
    std::function<
        bool(
            const KWinWindowCommand
                &command)>;
