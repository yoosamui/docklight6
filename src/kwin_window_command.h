#pragma once

#include "managed_window.h"

#include <functional>

enum class KWinWindowCommandType
{
    ACTIVATE,
    RAISE,
    CLOSE,
    SET_MINIMIZED,
    SET_MAXIMIZED
};

struct KWinWindowCommand
{
    WindowId window_id;

    KWinWindowCommandType type =
        KWinWindowCommandType::ACTIVATE;

    bool state = false;
};

using KWinWindowCommandHandler =
    std::function<
        bool(
            const KWinWindowCommand
                &command)>;
