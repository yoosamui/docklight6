#!/usr/bin/env node

"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const helperPath = path.resolve(
    __dirname,
    "../../gnome/docklight-window-integration@docklight6/placement.js");
const installerPath = path.resolve(
    __dirname,
    "../../gnome/install-window-integration.sh");
const extensionPath = path.resolve(
    __dirname,
    "../../gnome/docklight-window-integration@docklight6/extension.js");
const autohideControllerPath = path.resolve(
    __dirname,
    "../autohide/dock_autohide_controller.cpp");

// gnome-extensions pack only bundles its conventional entry points unless
// imported modules are explicitly listed as extra sources. Keep the package
// contract beside the geometry tests because missing this helper disables all
// GNOME placement before any edge calculation can run.
assert.match(
    fs.readFileSync(extensionPath, "utf8"),
    /from ['"]\.\/placement\.js['"]/);
assert.match(
    fs.readFileSync(installerPath, "utf8"),
    /--extra-source=placement\.js/);

const extensionSource = fs.readFileSync(extensionPath, "utf8");
const autohideControllerSource = fs.readFileSync(
    autohideControllerPath, "utf8");
assert.match(
    autohideControllerSource,
    /hide_now\([\s\S]*?\)[\s\S]*?if \(uses_shell_reveal_trigger\(\)\)[\s\S]*?set_shell_dock_hidden\(true\);\s*return;/,
    "GNOME autohide must keep the placed dock mapped instead of remapping at the centre");
assert.match(
    autohideControllerSource,
    /set_shell_dock_hidden\(bool hidden\)[\s\S]*?set_pass_through\(hidden\)[\s\S]*?input_shape_combine_region[\s\S]*?set_dock_hidden\(hidden\)/,
    "the persistent hidden dock must be input-pass-through and published to Shell");
assert.match(
    extensionSource,
    /_animateDockVisibility\(hidden[\s\S]*?GLib\.timeout_add\([\s\S]*?progress \* progress \* progress[\s\S]*?Math\.pow\(1 - progress, 3\)/,
    "GNOME must animate the already-placed compositor actor at the screen edge");
assert.match(
    extensionSource,
    /_finishDockTransition\(\)[\s\S]*?_animateDockVisibility\(\s*this\._dockHidden, this\._dockActor\)/,
    "placement completion must not reveal a dock that autohid while placement settled");
assert.match(
    autohideControllerSource,
    /request_reveal\(\)[\s\S]*?m_shell_edge_reveal = true[\s\S]*?schedule_hide\(false\)/,
    "an edge-only reveal must auto-hide unless the pointer moves into the dock");
assert.match(
    autohideControllerSource,
    /reveal\(\)[\s\S]*?if \(uses_shell_reveal_trigger\(\)\)[\s\S]*?set_shell_dock_hidden\(false\);/,
    "a Shell reveal must restore the existing mapped dock surface");
assert.match(
    extensionSource,
    /signalName === 'DockHiddenChanged'[\s\S]*?this\._dockHidden = Boolean[\s\S]*?this\._updateDockRevealActor\(\)/,
    "Shell must drive its reveal strip from the application's hidden state");
assert.match(
    extensionSource,
    /_placeAuxiliaryWindow[\s\S]*?move_frame\(false, target\.x, target\.y\)/,
    "private reveal surfaces must not receive Mutter's interactive edge inset");
assert.match(
    extensionSource,
    /\['size-changed', \(\) => this\._placeAuxiliaryWindow\(window\)\]/,
    "private reveal surfaces must be replaced after their final GTK allocation");
assert.match(
    extensionSource,
    /\['position-changed', \(\) => this\._placeAuxiliaryWindow\(window\)\]/,
    "private reveal surfaces must resist Mutter's late initial placement");
assert.match(
    extensionSource,
    /if \(this\._isAuxiliaryWindow\(window\)\)\s*this\._beginAuxiliaryTransition\(window, actor\)/,
    "ordinary Wayland auxiliary actors must be hidden during their first map");
assert.match(
    extensionSource,
    /_beginAuxiliaryTransition\(window, actor = null\)[\s\S]*?compositorActor\.set_opacity\(0\)/,
    "a provisional centred auxiliary frame must not enter the scene");
assert.match(
    extensionSource,
    /_beginDockTransition\(\)[\s\S]*?actor\.remove_all_transitions\(\)[\s\S]*?actor\.scale_x = 1[\s\S]*?actor\.scale_y = 1[\s\S]*?actor\.set_opacity\(0\)/,
    "GNOME's normal-window map animation must not expose the centred dock actor");
assert.match(
    extensionSource,
    /rect\.x === target\.x && rect\.y === target\.y[\s\S]*?_finishAuxiliaryTransition\(window\)/,
    "auxiliary opacity must be restored only after placement is committed");
assert.match(
    extensionSource,
    /_considerAuxiliaryWindow\(window\)[\s\S]*?if \(this\._dockWindow === window\)\s*this\._clearDockWindow\(\)/,
    "late auxiliary metadata must undo provisional dock classification");
const auxiliaryPlacementSource = extensionSource.match(
    /_placeAuxiliaryWindow\(window, position = null\) \{[\s\S]*?\n    \}\n\n    _clearAuxiliaryWindow/)[0];
assert.doesNotMatch(
    auxiliaryPlacementSource,
    /placeDockInWorkArea/,
    "private reveal coordinates are already resolved by the application");
assert.match(
    extensionSource,
    /const dockStrut = this\._dockStrut;\s*this\._dockStrut = null;\s*try \{/,
    "strut teardown must clear its reference before Shell can dispose it");
assert.match(
    extensionSource,
    /addChrome\(this\._dockRevealActor,[\s\S]*?affectsInputRegion: true/,
    "GNOME autohide must use a Shell-owned reactive edge strip");
assert.match(
    extensionSource,
    /this\._dockAutohide === 'none' \|\| !this\._dockHidden[\s\S]*?this\._call\('RequestDockReveal'/,
    "the Shell edge strip must request reveal only while the persistent dock is hidden");
assert.match(
    extensionSource,
    /const committed = isDockPlacementCommitted\([\s\S]*?actor\.translation_x = committed[\s\S]*?x - rect\.x[\s\S]*?actor\.translation_y = committed[\s\S]*?y - rect\.y/,
    "an asynchronously remapped dock must be painted at its edge, never at Mutter's provisional centre");
assert.match(
    extensionSource,
    /placement\.edge === 'bottom'[\s\S]*?monitor\.y \+ monitor\.height - revealSize/,
    "the bottom reveal strip must touch the physical monitor edge");

const source = fs.readFileSync(helperPath, "utf8")
    .replaceAll("export function ", "function ") +
    "\nthis.testApi = {calculateDockStrut, inferDockEdge, " +
    "isDockPlacementCommitted, parseAuxiliaryPosition, placeDockInWorkArea};";
const context = {};
vm.createContext(context);
vm.runInContext(source, context, {filename: helperPath});

const {
    calculateDockStrut,
    inferDockEdge,
    isDockPlacementCommitted,
    parseAuxiliaryPosition,
    placeDockInWorkArea,
} = context.testApi;
const primary = {x: 0, y: 0, width: 1170, height: 1080};
const primaryWorkArea = {x: 0, y: 32, width: 1170, height: 1048};

assert.strictEqual(isDockPlacementCommitted(
    {x: 385, y: 508}, {x: 385, y: 1016}, {x: 0, y: 0}), false);
assert.strictEqual(isDockPlacementCommitted(
    {x: 385, y: 1016}, {x: 385, y: 1016}, {x: 0, y: 0}), true);
assert.strictEqual(isDockPlacementCommitted(
    {x: 385, y: 952}, {x: 385, y: 1016}, {x: 0, y: 64}), true);

assert.deepStrictEqual(
    {...parseAuxiliaryPosition("Docklight 6 Reveal@1168,340")},
    {x: 1168, y: 340});
assert.deepStrictEqual(
    {...parseAuxiliaryPosition("Docklight 6 Reveal@-2,-64")},
    {x: -2, y: -64});
assert.strictEqual(parseAuxiliaryPosition("Docklight 6 Dock"), null);

assert.strictEqual(
    inferDockEdge(primary, {x: 385, y: 0, width: 400, height: 64}),
    "top");
assert.deepStrictEqual(
    {...placeDockInWorkArea(
        primary,
        primaryWorkArea,
        {x: 385, y: 0, width: 400, height: 64})},
    {x: 385, y: 32, width: 400, height: 64, edge: "top"});
assert.deepStrictEqual(
    {...placeDockInWorkArea(
        primary,
        primaryWorkArea,
        {x: 385, y: 0, width: 400, height: 2})},
    {x: 385, y: 32, width: 400, height: 2, edge: "top"});
assert.deepStrictEqual(
    JSON.parse(JSON.stringify(calculateDockStrut(
        primary,
        {x: 385, y: 32, width: 400, height: 64}))),
    {
        x: 0,
        y: 0,
        width: 1170,
        height: 96,
        actorOffset: {x: 0, y: -64},
    });

assert.deepStrictEqual(
    {...placeDockInWorkArea(
        primary,
        primaryWorkArea,
        {x: 1106, y: 340, width: 64, height: 400})},
    {x: 1106, y: 340, width: 64, height: 400, edge: "right"});

const rightDockWorkArea = {x: 0, y: 32, width: 1106, height: 1048};
assert.deepStrictEqual(
    {...placeDockInWorkArea(
        primary,
        rightDockWorkArea,
        {x: 1106, y: 340, width: 64, height: 400})},
    {x: 1042, y: 340, width: 64, height: 400, edge: "right"});
assert.deepStrictEqual(
    JSON.parse(JSON.stringify(calculateDockStrut(
        primary,
        {x: 1042, y: 340, width: 64, height: 400}))),
    {
        x: 1042,
        y: 0,
        width: 128,
        height: 1080,
        actorOffset: {x: 64, y: 0},
    });

const secondary = {x: 1170, y: 164, width: 877, height: 916};
assert.deepStrictEqual(
    {...placeDockInWorkArea(
        secondary,
        secondary,
        {x: 1170, y: 422, width: 64, height: 400})},
    {x: 1170, y: 422, width: 64, height: 400, edge: "left"});

assert.deepStrictEqual(
    {...placeDockInWorkArea(
        primary,
        primaryWorkArea,
        {x: 385, y: 1016, width: 400, height: 64})},
    {x: 385, y: 1016, width: 400, height: 64, edge: "bottom"});

console.log("GNOME placement tests passed");
