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
    desktopFileName) {
    return {
        internalId: {
            toString: () => identifier
        },
        desktopFileName,
        caption: "Home",
        icon: {
            name: () =>
                "system-file-manager"
        },
        pid: 1234,
        minimized: false,
        skipTaskbar: false,
        frameGeometry: {
            x: 10,
            y: 20,
            width: 800,
            height: 600
        },
        activities: [
            "activity,one"
        ],
        desktops: [
            {
                id: "desktop,one"
            }
        ],
        managed: true,
        deleted: false,
        popupWindow: false,
        outline: false,
        desktopWindow: false,
        dock: false,
        frameGeometryChanged:
            new Signal(),
        skipTaskbarChanged:
            new Signal(),
        iconChanged:
            new Signal(),
        desktopsChanged:
            new Signal(),
        activitiesChanged:
            new Signal(),
        minimizedChanged:
            new Signal(),
        captionChanged:
            new Signal(),
        maximizedChanged:
            new Signal(),
        desktopFileNameChanged:
            new Signal(),
        stackingOrderChanged:
            new Signal()
    };
}

const managedWindow =
    createWindow(
        "window-1",
        "org.kde.dolphin");

const dockWindow =
    createWindow(
        "dock-window",
        "org.docklight6");

dockWindow.dock = true;

const workspace = {
    stackingOrder: [
        managedWindow,
        dockWindow
    ],
    activeWindow: managedWindow,
    windowAdded: new Signal(),
    windowRemoved: new Signal(),
    windowActivated: new Signal()
};

const calls = [];

function callDBus(
    service,
    objectPath,
    interfaceName,
    methodName,
    ...arguments_) {
    const callback =
        typeof arguments_[
            arguments_.length - 1] ===
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

const scriptPath =
    path.resolve(
        __dirname,
        "../../kwin/" +
            "org.docklight6.windowintegration/" +
            "contents/code/main.js");

const metadataPath =
    path.resolve(
        __dirname,
        "../../kwin/" +
            "org.docklight6.windowintegration/" +
            "metadata.json");

const metadata =
    JSON.parse(
        fs.readFileSync(
            metadataPath,
            "utf8"));

assert.strictEqual(
    metadata.KPlugin.Id,
    "org.docklight6.windowintegration");
assert.strictEqual(
    metadata["KPackageStructure"],
    "KWin/Script");

const context = {
    Boolean,
    Math,
    Number,
    String,
    encodeURIComponent,
    isFinite,
    callDBus,
    workspace
};

vm.createContext(context);
vm.runInContext(
    fs.readFileSync(
        scriptPath,
        "utf8"),
    context,
    {
        filename: scriptPath
    });

assert.strictEqual(
    calls.length,
    1);
assert.strictEqual(
    calls[0].methodName,
    "Register");
assert.deepStrictEqual(
    calls[0].arguments,
    ["2"]);

calls[0].callback(
    true,
    "2");

const beginSnapshot =
    calls.find(
        call =>
            call.methodName ===
            "BeginSnapshot");

const stagedWindows =
    calls.filter(
        call =>
            call.methodName ===
            "StageWindow");

const commitSnapshot =
    calls.find(
        call =>
            call.methodName ===
            "CommitSnapshot");

assert(beginSnapshot);
assert.strictEqual(
    stagedWindows.length,
    1);
assert(commitSnapshot);

for (const value of
     stagedWindows[0].arguments) {
    assert.strictEqual(
        typeof value,
        "string");
}

assert.strictEqual(
    stagedWindows[0].arguments.length,
    2);

const stagedWindowPayload =
    stagedWindows[0]
        .arguments[1]
        .split(",")
        .map(decodeURIComponent);

assert.strictEqual(
    stagedWindowPayload[0],
    "window-1");
assert.strictEqual(
    stagedWindowPayload[12],
    "activity%2Cone");
assert.strictEqual(
    stagedWindowPayload[13],
    "desktop%2Cone");
assert.deepStrictEqual(
    commitSnapshot.arguments,
    [
        "1",
        "window-1",
        "window-1"
    ]);

managedWindow.caption =
    "Downloads";
managedWindow.captionChanged.emit();

const windowUpdate =
    calls[calls.length - 1];

assert.strictEqual(
    windowUpdate.methodName,
    "PublishWindow");
assert.strictEqual(
    windowUpdate.arguments[1]
        .split(",")
        .map(decodeURIComponent)[2],
    "Downloads");

workspace.windowActivated.emit(
    managedWindow);

assert.strictEqual(
    calls[calls.length - 1].methodName,
    "PublishActiveWindow");

managedWindow.stackingOrderChanged.emit();

assert.strictEqual(
    calls[calls.length - 1].methodName,
    "PublishStackingOrder");

workspace.stackingOrder = [
    dockWindow
];
workspace.windowRemoved.emit(
    managedWindow);

assert(
    calls.some(
        call =>
            call.methodName ===
            "PublishWindowRemoved"));

const previousRegisterCount =
    calls.filter(
        call =>
            call.methodName ===
            "Register")
        .length;

windowUpdate.callback(false);

assert.strictEqual(
    calls.filter(
        call =>
            call.methodName ===
            "Register")
        .length,
    previousRegisterCount + 1);
