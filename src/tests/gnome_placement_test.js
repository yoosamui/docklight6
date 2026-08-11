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
    /hide_now\([\s\S]*?\)[\s\S]*?if \(uses_shell_reveal_trigger\(\)\)[\s\S]*?request_shell_visibility\(true\);\s*return;/,
    "GNOME autohide must keep the placed dock mapped instead of remapping at the centre");
assert.match(
    autohideControllerSource,
    /finish_shell_animation\([\s\S]*?hidden[\s\S]*?ShellDockState::hidden[\s\S]*?set_shell_input_passthrough\(true\)/,
    "input pass-through must begin only after Shell completes the hide animation");
assert.match(
    extensionSource,
    /_startDockVisibilityTransition\(hidden[\s\S]*?actor\.ease\([\s\S]*?EASE_IN_QUAD[\s\S]*?EASE_OUT_QUAD/,
    "GNOME must use a native compositor transition at the screen edge");
assert.match(
    extensionSource,
    /remainingDistance < 0\.5[\s\S]*?completeTransition\(\)[\s\S]*?return;[\s\S]*?actor\.ease/,
    "an already-positioned actor must still complete the visibility state change");
assert.match(
    extensionSource,
    /completionSource = GLib\.timeout_add\([\s\S]*?duration \+ 32[\s\S]*?completeTransition\(\)[\s\S]*?actor\.ease/,
    "a compositor callback omission must not strand the visibility state machine");
assert.match(
    extensionSource,
    /_finishDockTransition\(\)[\s\S]*?_startDockVisibilityTransition\(\s*this\._dockHidden, this\._dockActor\)/,
    "placement completion must not reveal a dock that autohid while placement settled");
assert.match(
    autohideControllerSource,
    /set_shell_pointer_inside\([\s\S]*?m_pointer_inside = inside[\s\S]*?ShellDockState::hiding[\s\S]*?reveal\(\)/,
    "authoritative Shell pointer entry must reverse an in-progress hide");
assert.match(
    autohideControllerSource,
    /reveal\(\)[\s\S]*?if \(uses_shell_reveal_trigger\(\)\)[\s\S]*?request_shell_visibility\(false\);/,
    "a Shell reveal must restore the existing mapped dock surface");
assert.match(
    extensionSource,
    /signalName === 'DockHiddenChanged'[\s\S]*?this\._dockHidden = Boolean[\s\S]*?this\._startDockVisibilityTransition/,
    "Shell must drive its reveal strip from the application's hidden state");
assert.match(
    extensionSource,
    /global\.backend\.get_cursor_tracker\(\)[\s\S]*?_cursorTracker\.get_pointer\(\)[\s\S]*?_pointerPosition[\s\S]*?_publishDockPointerInside[\s\S]*?PublishDockPointerInside/,
    "Shell must publish compositor-global pointer presence instead of relying on GTK crossing events");
assert.doesNotMatch(
    extensionSource,
    /global\.get_pointer/,
    "pointer tracking must not use the removed GNOME Shell global pointer API");
assert.doesNotMatch(
    autohideControllerSource,
    /animate_shell_opacity/,
    "GNOME visibility must have only one animation owner");
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
    /this\._dockAutohide === 'none'[\s\S]*?this\._dockVisibilityState !== 'hidden'[\s\S]*?this\._call\('RequestDockReveal'/,
    "the Shell edge strip must request reveal only while the persistent dock is hidden");
assert.match(
    extensionSource,
    /const committed = isDockPlacementCommitted\([\s\S]*?if \(committed\)[\s\S]*?actor\.translation_x = actorOffset\.x[\s\S]*?actor\.translation_y = actorOffset\.y[\s\S]*?actor\.translation_x = x - rect\.x[\s\S]*?actor\.translation_y = y - rect\.y/,
    "an asynchronously remapped dock must be painted at its edge, never at Mutter's provisional centre");
assert.match(
    extensionSource,
    /placement\.edge === 'bottom'[\s\S]*?monitor\.y \+ monitor\.height - revealSize/,
    "the bottom reveal strip must touch the physical monitor edge");

const source = fs.readFileSync(helperPath, "utf8")
    .replaceAll("export function ", "function ") +
    "\nthis.testApi = {calculateDockStrut, inferDockEdge, " +
    "isDockPlacementCommitted, isPointerInsideDockInterior, " +
    "parseAuxiliaryPosition, placeDockInWorkArea};";
const context = {};
vm.createContext(context);
vm.runInContext(source, context, {filename: helperPath});

const {
    calculateDockStrut,
    inferDockEdge,
    isDockPlacementCommitted,
    isPointerInsideDockInterior,
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

const pointerFixtures = [
    {
        placement: {x: 300, y: 32, width: 400, height: 64, edge: "top"},
        edge: [500, 32],
        interior: [500, 40],
    },
    {
        placement: {x: 300, y: 1016, width: 400, height: 64, edge: "bottom"},
        edge: [500, 1079],
        interior: [500, 1070],
    },
    {
        placement: {x: 0, y: 340, width: 64, height: 400, edge: "left"},
        edge: [0, 500],
        interior: [8, 500],
    },
    {
        placement: {x: 1106, y: 340, width: 64, height: 400, edge: "right"},
        edge: [1169, 500],
        interior: [1160, 500],
    },
];
for (const fixture of pointerFixtures) {
    assert.strictEqual(isPointerInsideDockInterior(
        fixture.placement, ...fixture.edge), false);
    assert.strictEqual(isPointerInsideDockInterior(
        fixture.placement, ...fixture.interior), true);
}
assert.strictEqual(isPointerInsideDockInterior(
    pointerFixtures[1].placement, 299, 1070), false);

console.log("GNOME placement tests passed");
