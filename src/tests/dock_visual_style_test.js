#!/usr/bin/env node

"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");

const surfacePath = path.resolve(
    __dirname,
    "../dock/dock_window_surface.cpp");
const surfaceSource = fs.readFileSync(surfacePath, "utf8");

assert.match(
    surfaceSource,
    /gradient_background\(\)[\s\S]*?background-color: @theme_bg_color;[\s\S]*?linear-gradient\([\s\S]*?shade\(@theme_bg_color, 0\.70\)[\s\S]*?shade\(@theme_bg_color, 1\.20\)/,
    "the dock gradient must derive every color from the active GTK theme");
assert.doesNotMatch(
    surfaceSource,
    /linear-gradient\([\s\S]*?#(?:000000|413f3f)/,
    "the dock gradient must not retain its old hardcoded colors");
assert.match(
    surfaceSource,
    /: " background-color: @theme_bg_color;"[\s\S]*?" background-image: none;"/,
    "disabling the gradient must use the active GTK theme background");

console.log("Dock visual style tests passed");
