#!/usr/bin/env node

"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");

const backendHeaderSource = fs.readFileSync(
    path.resolve(
        __dirname,
        "../integrations/x11/marco_window_backend.h"),
    "utf8");
const backendSource = fs.readFileSync(
    path.resolve(
        __dirname,
        "../integrations/x11/marco_window_backend.cpp"),
    "utf8");
const ewmhSource = fs.readFileSync(
    path.resolve(
        __dirname,
        "../integrations/x11/ewmh_window_backend.cpp"),
    "utf8");
const minimizedOverride = backendSource.match(
    /MarcoWindowBackend::set_window_minimized_override\([\s\S]*?\n\}/)?.[0];
const presentWindows = ewmhSource.match(
    /EwmhWindowBackend::present_windows\([\s\S]*?\n\}/)?.[0];

assert.match(
    backendHeaderSource,
    /set_window_minimized_override\([\s\S]*?override;/,
    "Marco must isolate Metacity-compatible restore behavior in its backend");
assert.ok(
    minimizedOverride,
    "Marco must provide its grouped-window restore override");
assert.match(
    minimizedOverride,
    new RegExp(
        "if \\(minimized\\)\\s*return std::nullopt;" +
        "[\\s\\S]*?wnck_window_get_workspace\\(window\\)" +
        "[\\s\\S]*?XMapWindow\\(" +
        "[\\s\\S]*?wnck_window_get_xid\\(window\\)" +
        "[\\s\\S]*?wnck_window_move_to_workspace\\("),
    "Marco must map restored windows and reassert their original workspace");
assert.match(
    minimizedOverride,
    new RegExp(
        "gdk_x11_display_error_trap_push" +
        "[\\s\\S]*?XMapWindow" +
        "[\\s\\S]*?XFlush" +
        "[\\s\\S]*?gdk_x11_display_error_trap_pop"),
    "Marco must contain asynchronous X11 mapping errors");
assert.doesNotMatch(
    minimizedOverride,
    /wnck_window_unminimize|workspace_activate|window_activate|defer_activation/,
    "Marco restore must not activate every grouped window through libwnck");
assert.ok(
    presentWindows,
    "The generic EWMH grouped presentation path must remain available");
assert.match(
    presentWindows,
    /set_window_minimized_override\([\s\S]*?wnck_window_activate\(target, timestamp\)/,
    "Grouped presentation must restore through the WM override before one activation");

console.log("Marco restore tests passed");
