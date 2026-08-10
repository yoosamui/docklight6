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
    /actor\.hide\(\);\s*this\._expectDockRemap\(\);\s*this\._call\('RequestDockReveal'/,
    "the Shell edge strip must identify the expected dock before it remaps");
assert.match(
    extensionSource,
    /\(!this\._dockDiscoveredOnce \|\| this\._dockRevealPending\)/,
    "an expected autohide remap must use early application-id recognition");
assert.match(
    extensionSource,
    /this\._dockWindow = window;[\s\S]*?this\._clearDockRevealExpectation\(\)/,
    "dock discovery must consume the short-lived remap expectation");
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
