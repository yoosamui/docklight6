#!/usr/bin/env node

"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const helperPath = path.resolve(
    __dirname,
    "../../gnome/docklight-window-integration@docklight6/placement.js");
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
