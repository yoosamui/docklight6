#!/usr/bin/env node

"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");

const backendHeaderSource = fs.readFileSync(
    path.resolve(
        __dirname,
        "../integrations/x11/openbox_window_backend.h"),
    "utf8");
const backendSource = fs.readFileSync(
    path.resolve(
        __dirname,
        "../integrations/x11/openbox_window_backend.cpp"),
    "utf8");
const groupHide = backendSource.match(
    /OpenboxWindowBackend::hide_windows\([\s\S]*?\n\}/)?.[0];
const minimizedOverride = backendSource.match(
    /OpenboxWindowBackend::set_window_minimized_override\([\s\S]*?\n\}/)?.[0];
const hiddenState = backendSource.match(
    /OpenboxWindowBackend::set_windows_hidden\([\s\S]*?\n\}/)?.[0];

assert.match(
    backendHeaderSource,
    /bool hide_windows\([\s\S]*?override;/,
    "Openbox must isolate grouped minimize behavior in its backend");
assert.ok(
    groupHide,
    "Openbox must provide its grouped minimize override");
assert.match(
    groupHide,
    /for \(const auto &window_id[\s\S]*?find_window\(window_id\)[\s\S]*?return false;[\s\S]*?windows\.push_back\(window\)[\s\S]*?set_windows_hidden/,
    "Openbox must resolve the complete group before sending requests");
assert.match(
    groupHide,
    /set_windows_hidden\([\s\S]*?windows,[\s\S]*?true\)/,
    "Openbox must hide the validated group in one state batch");
assert.match(
    backendHeaderSource,
    /set_window_minimized_override\([\s\S]*?override;/,
    "Openbox must isolate non-activating restore behavior in its backend");
assert.ok(
    minimizedOverride,
    "Openbox must provide its minimize-state override");
assert.match(
    minimizedOverride,
    /set_windows_hidden\([\s\S]*?\{window\},[\s\S]*?minimized\)/,
    "Openbox must add or remove HIDDEN without activating each window");
assert.doesNotMatch(
    minimizedOverride,
    /wnck_window_unminimize|wnck_window_activate|workspace_activate|defer_activation/,
    "Openbox group restore must not activate or change workspaces per window");
assert.ok(
    hiddenState,
    "Openbox must provide its batched HIDDEN-state request helper");
assert.match(
    hiddenState,
    /"_NET_WM_STATE"[\s\S]*?"_NET_WM_STATE_HIDDEN"/,
    "Openbox must use its EWMH hidden-state path");
assert.match(
    hiddenState,
    /event\.xclient\.data\.l\[0\] =[\s\S]*?hidden[\s\S]*?\? 1[\s\S]*?: 0;[\s\S]*?event\.xclient\.data\.l\[3\] = 2;/,
    "Openbox must add or remove HIDDEN as a pager action");
assert.match(
    hiddenState,
    /for \(auto \*window : windows\)[\s\S]*?XSendEvent\([\s\S]*?SubstructureRedirectMask[\s\S]*?SubstructureNotifyMask[\s\S]*?XFlush/,
    "Openbox must dispatch every group member before flushing the batch");
assert.match(
    hiddenState,
    /gdk_x11_display_error_trap_push[\s\S]*?XSendEvent[\s\S]*?gdk_x11_display_error_trap_pop/,
    "Openbox must contain asynchronous X11 request errors");
assert.doesNotMatch(
    groupHide,
    /wnck_window_minimize|EwmhWindowBackend::hide_windows/,
    "Openbox grouped minimize must not fall back to independent libwnck iconify calls");

console.log("Openbox grouped minimize tests passed");
