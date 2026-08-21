#!/usr/bin/env node

"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");

const backendHeaderSource = fs.readFileSync(
    path.resolve(
        __dirname,
        "../integrations/x11/xfwm4_window_backend.h"),
    "utf8");
const backendSource = fs.readFileSync(
    path.resolve(
        __dirname,
        "../integrations/x11/xfwm4_window_backend.cpp"),
    "utf8");
const minimizedOverride = backendSource.match(
    /Xfwm4WindowBackend::set_window_minimized_override\([\s\S]*?\n\}/)?.[0];

assert.match(
    backendHeaderSource,
    /set_window_minimized_override\([\s\S]*?override;/,
    "XFWM must isolate workspace-preserving restore behavior in its backend");
assert.ok(
    minimizedOverride,
    "XFWM must provide its workspace-preserving restore override");
assert.match(
    minimizedOverride,
    new RegExp(
        "if \\(minimized\\)\\s*return std::nullopt;" +
        "[\\s\\S]*?XMapWindow\\(" +
        "[\\s\\S]*?wnck_window_get_xid\\(window\\)"),
    "XFWM must map restored windows without activating them");
assert.match(
    minimizedOverride,
    new RegExp(
        "gdk_x11_display_error_trap_push" +
        "[\\s\\S]*?XMapWindow" +
        "[\\s\\S]*?XFlush" +
        "[\\s\\S]*?gdk_x11_display_error_trap_pop"),
    "XFWM must contain asynchronous X11 mapping errors");
assert.doesNotMatch(
    minimizedOverride,
    /wnck_window_unminimize|workspace_activate|window_activate|defer_activation/,
    "XFWM restore must not activate each window through libwnck");

console.log("XFWM restore tests passed");
