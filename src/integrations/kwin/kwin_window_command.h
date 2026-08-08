// ------------------------------------------------------------
// Docklight 6.0
//
// Copyright (c) 2018-2026 yoosamui
// Author and Maintainer: yoosamui
// ------------------------------------------------------------
//
// File:
// kwin_window_command.h
//
// Purpose:
// Declares plain command values passed from the generic window API to KWin
// transport.
//
// Responsibilities:
// - Identify the requested action and target window.
// - Carry optional application-group members.
// - Define the transport-neutral command callback type.
//
// Dependencies and ownership:
// Commands own copied identifiers and group values; handlers define their
// own capture ownership.
//
// Design notes:
// The value type does not prescribe D-Bus transport behavior.
//
// ------------------------------------------------------------

#pragma once

#include "windowing/managed_window.h"

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
