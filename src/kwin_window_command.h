// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// Defines the plain command values passed from the generic window API
// to the KWin integration transport.
//
// Commands own their identifiers and group members by value. The
// handler type does not prescribe transport or object ownership.
//
// ------------------------------------------------------------

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
