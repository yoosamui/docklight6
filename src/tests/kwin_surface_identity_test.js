#!/usr/bin/env node

"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

class Signal {
    constructor() {
        this.callbacks = [];
    }

    connect(callback) {
        this.callbacks.push(callback);
    }

    emit(...arguments_) {
        for (const callback of this.callbacks)
            callback(...arguments_);
    }
}

function createWindow(
    identifier,
    role,
    geometry) {
    return {
        internalId: {
            toString: () => identifier
        },
        resourceName: "docklight6",
        resourceClass: "Docklight6",
        windowRole: role,
        desktopFileName: "org.docklight6",
        caption: identifier,
        icon: null,
        pid: 4242,
        minimized: false,
        maximized: false,
        keepAbove: false,
        skipTaskbar: true,
        frameGeometry: geometry,
        activities: [],
        desktops: [],
        managed: true,
        deleted: false,
        popupWindow: false,
        tooltip: false,
        outline: false,
        desktopWindow: false,
        dock: false,
        x11Client: true,
        frameGeometryChanged: new Signal(),
        outputChanged: new Signal(),
        windowShown: new Signal(),
        windowHidden: new Signal(),
        skipTaskbarChanged: new Signal(),
        keepAboveChanged: new Signal(),
        iconChanged: new Signal(),
        desktopsChanged: new Signal(),
        activitiesChanged: new Signal(),
        minimizedChanged: new Signal(),
        captionChanged: new Signal(),
        maximizedChanged: new Signal(),
        desktopFileNameChanged: new Signal(),
        resourceClassChanged: new Signal(),
        resourceNameChanged: new Signal(),
        stackingOrderChanged: new Signal()
    };
}

const reveal = createWindow(
    "reveal",
    "",
    {x: 0, y: 44, width: 2, height: 1036});
reveal.dock = true;
reveal.x11Client = false;
reveal.layer = 9;

const preview = createWindow(
    "preview",
    "docklight6-preview",
    {x: 53, y: 300, width: 512, height: 400});
preview.keepAbove = true;

const tooltip = createWindow(
    "tooltip",
    "docklight6-tooltip",
    {x: 53, y: 500, width: 140, height: 32});
tooltip.tooltip = true;
tooltip.keepAbove = true;

const settings = createWindow(
    "settings",
    "docklight6-settings",
    {x: 600, y: 200, width: 460, height: 700});
settings.keepAbove = true;

const dock = createWindow(
    "dock",
    "",
    {x: 0, y: 44, width: 53, height: 1036});
dock.dock = true;
dock.x11Client = false;
dock.layer = 3;
dock.output = {
    geometry: {
        x: 0,
        y: 0,
        width: 1920,
        height: 1080
    }
};

const workspace = {
    // Mandatory auxiliary-first startup: the real dock does not exist yet.
    stackingOrder: [
        reveal,
        preview,
        tooltip,
        settings
    ],
    currentDesktop: {
        id: "desktop-one",
        x11DesktopNumber: 1
    },
    currentActivity: "activity-one",
    activeWindow: null,
    cursorPos: {x: 20, y: 400},
    clientArea() {
        return {
            x: 0,
            y: 44,
            width: 1920,
            height: 1036
        };
    },
    windowAdded: new Signal(),
    windowRemoved: new Signal(),
    windowActivated: new Signal(),
    currentDesktopChanged: new Signal(),
    cursorPosChanged: new Signal(),
    screenAdded: new Signal(),
    screenRemoved: new Signal()
};

const calls = [];

function callDBus(
    service,
    objectPath,
    interfaceName,
    methodName,
    ...arguments_) {
    const callback =
        typeof arguments_[arguments_.length - 1] ===
            "function"
            ? arguments_.pop()
            : null;

    calls.push({
        service,
        objectPath,
        interfaceName,
        methodName,
        arguments: arguments_,
        callback
    });
}

const sourceDirectory = path.resolve(
    __dirname,
    "..");
const scriptPath = path.resolve(
    sourceDirectory,
    "../kwin/" +
        "org.docklight6.windowintegration/" +
        "contents/code/main.js");
const identityHeaderPath = path.resolve(
    sourceDirectory,
    "presentation/docklight_surface_identity.h");

const scriptSource = fs.readFileSync(
    scriptPath,
    "utf8");
const identityHeader = fs.readFileSync(
    identityHeaderPath,
    "utf8");

// Architecture assertions keep the shared declaration and KWin's platform
// interpretation on the same stable semantic contract.
for (const [constant, value] of [
    ["DOCK_ROLE", "docklight6-dock"],
    ["REVEAL_ROLE", "docklight6-reveal"],
    ["TOOLTIP_ROLE", "docklight6-tooltip"],
    ["PREVIEW_ROLE", "docklight6-preview"],
    ["SETTINGS_ROLE", "docklight6-settings"],
    ["ABOUT_ROLE", "docklight6-about"],
    ["ICON_CHOOSER_ROLE", "docklight6-icon-chooser"],
    ["COMPOSITOR_WARNING_ROLE", "docklight6-compositor-warning"]
]) {
    assert.match(
        identityHeader,
        new RegExp(
            constant +
            "\\[\\]\\s*=\\s*\\n?\\s*\\\"" +
            value +
            "\\\""));
}

for (const [relativePath, constant] of [
    ["dock/dock_window.cpp", "DOCK_ROLE"],
    ["autohide/dock_reveal_window.cpp", "REVEAL_ROLE"],
    ["dock/dock_tooltip_window.cpp", "TOOLTIP_ROLE"],
    ["preview/dock_preview_window.cpp", "PREVIEW_ROLE"],
    ["dialogs/dock_settings_dialog.cpp", "SETTINGS_ROLE"],
    ["dialogs/dock_settings_dialog.cpp", "ICON_CHOOSER_ROLE"],
    ["dialogs/dock_about_dialog.cpp", "ABOUT_ROLE"],
    ["main.cpp", "COMPOSITOR_WARNING_ROLE"],
    ["dock/backends/layer_shell_dock_surface_backend.cpp",
        "DOCK_NAMESPACE"],
    ["autohide/dock_reveal_window.cpp", "REVEAL_NAMESPACE"],
    ["dock/dock_tooltip_window.cpp", "TOOLTIP_NAMESPACE"],
    ["preview/dock_preview_window.cpp", "PREVIEW_NAMESPACE"],
    ["dialogs/dock_about_dialog.cpp", "ABOUT_NAMESPACE"]
]) {
    const source = fs.readFileSync(
        path.resolve(sourceDirectory, relativePath),
        "utf8");
    assert.match(
        source,
        new RegExp(
            "DocklightSurfaceIdentity::\\s*" +
            constant));
}

const settingsDialogSource = fs.readFileSync(
    path.resolve(
        sourceDirectory,
        "dialogs/dock_settings_dialog.cpp"),
    "utf8");

// Settings must remain an ordinary decorated toplevel. A layer-shell parent
// prevents GtkColorButton's native chooser from establishing real transient
// modality and allows a second chooser to be opened behind the first.
assert.doesNotMatch(
    settingsDialogSource,
    /gtk_layer_init_for_window/);
assert.match(
    settingsDialogSource,
    /Gtk::ColorButton\s+indicator_color\s*;/);
assert.match(
    settingsDialogSource,
    /Gtk::ColorButton\s+preview_color\s*;/);

assert.match(
    scriptSource,
    /windowRole\s*===\s*\n?\s*DOCKLIGHT_MAIN_ROLE/);
assert.match(
    scriptSource,
    /Number\(window\.layer\)\s*===\s*\n?\s*KWIN_DOCK_LAYER/);
assert.doesNotMatch(
    scriptSource,
    /window\.skipTaskbar\s*&&\s*\n?\s*isDocklightWindow\(window\)/);

const context = {
    Boolean,
    Math,
    Number,
    String,
    encodeURIComponent,
    isFinite,
    KWin: {MaximizeArea: 3},
    callDBus,
    print() {},
    workspace
};

vm.createContext(context);
vm.runInContext(
    scriptSource,
    context,
    {filename: scriptPath});

assert.strictEqual(calls.length, 1);
assert.strictEqual(calls[0].methodName, "Register");
calls[0].callback(true, "10");

const geometryCalls = () =>
    calls.filter(
        call =>
            call.methodName ===
            "PublishDockSurfaceGeometry");
const placementCalls = () =>
    calls.filter(
        call =>
            call.methodName ===
            "GetDockPlacementGeometry");
const workAreaCalls = () =>
    calls.filter(
        call =>
            call.methodName ===
            "PublishDockWorkAreaGeometry");

// Reveal, preview, tooltip, and dialog surfaces existed during the snapshot,
// but none may become the dock geometry source.
assert.deepStrictEqual(
    geometryCalls().at(-1).arguments.slice(1),
    [0, 0, 0, 0]);
assert.strictEqual(placementCalls().length, 0);
assert.strictEqual(
    calls.filter(
        call => call.methodName === "StageWindow")
        .length,
    0);

workspace.stackingOrder.push(dock);
workspace.windowAdded.emit(dock);

assert.deepStrictEqual(
    geometryCalls().at(-1).arguments.slice(1),
    [
        dock.frameGeometry.x,
        dock.frameGeometry.y,
        dock.frameGeometry.width,
        dock.frameGeometry.height
    ]);

// Native layer-shell placement remains compositor-owned.
assert.strictEqual(placementCalls().length, 0);
dock.frameGeometryChanged.emit();
assert.deepStrictEqual(
    workAreaCalls().at(-1).arguments.slice(1),
    [0, 44, 1920, 1036]);

const dockGeometryCount = geometryCalls().length;

reveal.frameGeometryChanged.emit();
preview.frameGeometryChanged.emit();
tooltip.frameGeometryChanged.emit();
settings.frameGeometryChanged.emit();

assert.strictEqual(
    geometryCalls().length,
    dockGeometryCount);

dock.frameGeometry = {
    x: 1867,
    y: 44,
    width: 53,
    height: 1036
};
dock.frameGeometryChanged.emit();

assert.deepStrictEqual(
    geometryCalls().at(-1).arguments.slice(1),
    [1867, 44, 53, 1036]);

workspace.stackingOrder = [
    reveal,
    preview,
    tooltip,
    settings
];
const preRemovalWorkAreaCount =
    workAreaCalls().length;
workspace.windowRemoved.emit(dock);

assert.deepStrictEqual(
    geometryCalls().at(-1).arguments.slice(1),
    [0, 0, 0, 0]);
assert.strictEqual(
    workAreaCalls().length,
    preRemovalWorkAreaCount + 1);
assert.deepStrictEqual(
    workAreaCalls().at(-1).arguments.slice(1),
    [0, 44, 1920, 1036]);

console.log(
    "KWin surface identity tests passed");
