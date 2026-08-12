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
        maximized: false,
        closed: false,
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
                id: "desktop,one",
                x11DesktopNumber: 2
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
        resourceClassChanged:
            new Signal(),
        resourceNameChanged:
            new Signal(),
        stackingOrderChanged:
            new Signal(),
        closeWindow() {
            this.closed = true;
        },
        setMaximize(
            vertically,
            horizontally) {
            this.maximized =
                vertically === true &&
                horizontally === true;
        }
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

dockWindow.resourceName = "docklight6";
dockWindow.skipTaskbar = true;

const dockPopup =
    createWindow(
        "dock-popup",
        "org.docklight6");

dockPopup.resourceName = "docklight6";
dockPopup.skipTaskbar = true;
dockPopup.popupWindow = true;

const replacementDockWindow =
    createWindow(
        "replacement-dock-window",
        "org.docklight6");

replacementDockWindow.resourceName =
    "docklight6";
replacementDockWindow.skipTaskbar = true;
replacementDockWindow.frameGeometry = {
    x: 100,
    y: 700,
    width: 600,
    height: 64
};

const workspace = {
    stackingOrder: [
        managedWindow,
        dockWindow
    ],
    currentDesktop: {
        id: "desktop,two",
        x11DesktopNumber: 1
    },
    currentActivity: "activity,two",
    windowAdded: new Signal(),
    windowRemoved: new Signal(),
    windowActivated: new Signal(),
    currentDesktopChanged:
        new Signal(),
    activatedWindow: null,
    raisedWindow: null,
    raisedWindows: [],
    raiseWindow(window) {
        this.raisedWindow = window;
        this.raisedWindows.push(window);

        this.stackingOrder =
            this.stackingOrder.filter(
                candidate =>
                    candidate !== window);
        this.stackingOrder.push(window);

        for (const candidate of
             this.stackingOrder) {
            candidate
                .stackingOrderChanged
                .emit();
        }
    }
};

let activeWindow = managedWindow;

Object.defineProperty(
    workspace,
    "activeWindow",
    {
        configurable: true,
        get() {
            return activeWindow;
        },
        set(window) {
            activeWindow = window;
            this.activatedWindow =
                window;
        }
    });

const calls = [];
const messages = [];

function print(...arguments_) {
    messages.push(arguments_);
}

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
    print,
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
    ["8"]);

calls[0].callback(
    true,
    "8");

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

const dockSurfaceGeometry =
    calls.find(
        call =>
            call.methodName ===
            "PublishDockSurfaceGeometry");

assert(beginSnapshot);
assert.strictEqual(
    stagedWindows.length,
    1);
assert(commitSnapshot);
assert(dockSurfaceGeometry);
assert.deepStrictEqual(
    dockSurfaceGeometry.arguments,
    [
        "2",
        10,
        20,
        800,
        600
    ]);

const vlcWindow =
    createWindow(
        "vlc-window",
        "");

vlcWindow.resourceClass = "vlc";
workspace.stackingOrder.push(
    vlcWindow);
workspace.windowAdded.emit(
    vlcWindow);

const vlcUpdate =
    calls
        .filter(
            call =>
                call.methodName ===
                "PublishWindow")
        .at(-1);

assert.strictEqual(
    vlcUpdate.methodName,
    "PublishWindow");
assert.strictEqual(
    vlcUpdate.arguments[1]
        .split(",")
        .map(decodeURIComponent)[1],
    "vlc");

const pictureInPictureWindow =
    createWindow(
        "picture-in-picture",
        "org.kde.dolphin");
pictureInPictureWindow.skipTaskbar = true;
pictureInPictureWindow.keepAbove = true;
workspace.stackingOrder.push(
    pictureInPictureWindow);
workspace.windowAdded.emit(
    pictureInPictureWindow);

const pictureInPictureUpdate = calls
    .filter(call => call.methodName === "PublishWindow")
    .at(-1)
    .arguments[1]
    .split(",")
    .map(decodeURIComponent);

assert.strictEqual(
    pictureInPictureUpdate[7],
    "1");
assert.strictEqual(
    pictureInPictureUpdate[16],
    "1");

workspace.stackingOrder =
    workspace.stackingOrder.filter(
        window =>
            window !== vlcWindow &&
            window !== pictureInPictureWindow);
workspace.windowRemoved.emit(
    vlcWindow);
workspace.windowRemoved.emit(
    pictureInPictureWindow);

const dockGeometryCount =
    calls.filter(
        call =>
            call.methodName ===
            "PublishDockSurfaceGeometry")
        .length;

workspace.stackingOrder = [
    managedWindow,
    dockWindow,
    dockPopup
];
workspace.windowAdded.emit(
    dockPopup);
workspace.windowRemoved.emit(
    dockPopup);

assert.strictEqual(
    calls.filter(
        call =>
            call.methodName ===
            "PublishDockSurfaceGeometry")
        .length,
    dockGeometryCount);

workspace.stackingOrder = [
    managedWindow,
    replacementDockWindow
];
workspace.windowRemoved.emit(
    dockWindow);

const recoveredDockGeometry =
    calls
        .filter(
            call =>
                call.methodName ===
                "PublishDockSurfaceGeometry")
        .at(-1);

assert.deepStrictEqual(
    recoveredDockGeometry.arguments.slice(1),
    [
        100,
        700,
        600,
        64
    ]);

function deliverCommand(
    command,
    state,
    identifier = "window-1") {
    const wait =
        calls
            .filter(
                call =>
                    call.methodName ===
                    "WaitForCommand")
            .at(-1);

    assert(wait);
    assert(wait.callback);

    wait.callback(
        command,
        identifier,
        state);
}

deliverCommand(
    "activate",
    false);
assert.strictEqual(
    workspace.activatedWindow,
    managedWindow);
assert.strictEqual(
    workspace.currentDesktop,
    managedWindow.desktops[0]);
assert.strictEqual(
    workspace.currentActivity,
    managedWindow.activities[0]);

deliverCommand(
    "raise",
    false);
assert.strictEqual(
    workspace.raisedWindow,
    managedWindow);

const secondManagedWindow =
    createWindow(
        "window-2",
        "org.kde.dolphin");
secondManagedWindow.minimized = true;
workspace.stackingOrder.push(
    secondManagedWindow);
workspace.windowAdded.emit(
    secondManagedWindow);
workspace.raisedWindows = [];

const stackingPublishesBeforePresent =
    calls.filter(
        call =>
            call.methodName ===
            "PublishStackingOrder")
        .length;

deliverCommand(
    "present",
    false,
    "window-2,window-1");
assert.strictEqual(
    secondManagedWindow.minimized,
    false);
assert.deepStrictEqual(
    workspace.raisedWindows,
    [
        secondManagedWindow,
        managedWindow
    ]);
assert.strictEqual(
    workspace.activatedWindow,
    managedWindow);
assert.strictEqual(
    calls.filter(
        call =>
            call.methodName ===
            "PublishStackingOrder")
        .length,
    stackingPublishesBeforePresent + 1);

const waitCountBeforeHide =
    calls.filter(
        call =>
            call.methodName ===
            "WaitForCommand")
        .length;
let secondWindowMinimized = false;
let nextWaitReadyBeforeHide = false;

Object.defineProperty(
    secondManagedWindow,
    "minimized",
    {
        configurable: true,
        get() {
            return secondWindowMinimized;
        },
        set(value) {
            secondWindowMinimized =
                value;
            nextWaitReadyBeforeHide =
                calls.filter(
                    call =>
                        call.methodName ===
                        "WaitForCommand")
                    .length ===
                waitCountBeforeHide + 1;
        }
    });

deliverCommand(
    "hide",
    true,
    "window-2,window-1");
assert.strictEqual(
    secondManagedWindow.minimized,
    true);
assert.strictEqual(
    managedWindow.minimized,
    true);
assert.strictEqual(
    nextWaitReadyBeforeHide,
    true);

deliverCommand(
    "set-minimized",
    true);
assert.strictEqual(
    managedWindow.minimized,
    true);

deliverCommand(
    "set-maximized",
    true);
assert.strictEqual(
    managedWindow.maximized,
    true);

deliverCommand(
    "close",
    false);
assert.strictEqual(
    managedWindow.closed,
    true);

const waitCountBeforeFailure =
    calls.filter(
        call =>
            call.methodName ===
            "WaitForCommand")
        .length;

Object.defineProperty(
    workspace,
    "activeWindow",
    {
        configurable: true,
        get() {
            return activeWindow;
        },
        set() {
            throw new Error(
                "test command failure");
        }
    });

deliverCommand(
    "activate",
    false);

const waitCountAfterFailure =
    calls.filter(
        call =>
            call.methodName ===
            "WaitForCommand")
        .length;

assert.strictEqual(
    waitCountAfterFailure,
    waitCountBeforeFailure + 1);
assert.strictEqual(
    messages.length,
    1);
assert.strictEqual(
    messages[0][0],
    "Docklight command failed:");

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
assert.strictEqual(
    stagedWindowPayload[14],
    "2");
assert.strictEqual(
    stagedWindowPayload[15],
    "0");
assert.strictEqual(
    stagedWindowPayload[16],
    "0");
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

managedWindow.frameGeometry = {
    x: 120,
    y: 240,
    width: 640,
    height: 480
};
managedWindow.frameGeometryChanged.emit();

const geometryUpdate =
    calls[calls.length - 1];
const geometryPayload =
    geometryUpdate.arguments[1]
        .split(",")
        .map(decodeURIComponent);

assert.strictEqual(
    geometryUpdate.methodName,
    "PublishWindow");
assert.deepStrictEqual(
    geometryPayload.slice(8, 12),
    ["120", "240", "640", "480"]);

const activePublishCount =
    calls.filter(
        call =>
            call.methodName ===
                "PublishActiveWindow")
        .length;

activeWindow = null;
workspace.windowActivated.emit(
    null);

assert.strictEqual(
    calls.filter(
        call =>
            call.methodName ===
                "PublishActiveWindow")
        .length,
    activePublishCount);

activeWindow = dockWindow;
workspace.windowActivated.emit(
    dockWindow);

assert.strictEqual(
    calls.filter(
        call =>
            call.methodName ===
                "PublishActiveWindow")
        .length,
    activePublishCount);

activeWindow = managedWindow;
workspace.windowActivated.emit(
    managedWindow);

assert.strictEqual(
    calls[calls.length - 1].methodName,
    "PublishActiveWindow");

workspace.currentDesktop =
    managedWindow.desktops[0];
workspace.currentDesktopChanged.emit(
    {
        id: "desktop,two",
        x11DesktopNumber: 1
    },
    workspace.currentDesktop);

const desktopUpdate =
    calls
        .filter(
            call =>
                call.methodName ===
                "PublishCurrentDesktop")
        .at(-1);

assert.strictEqual(
    desktopUpdate.arguments[1],
    "desktop,one");
assert.strictEqual(
    desktopUpdate.arguments[2],
    2);

managedWindow.stackingOrderChanged.emit();

assert.strictEqual(
    calls.filter(
        call =>
            call.methodName ===
            "PublishStackingOrder")
        .length,
    stackingPublishesBeforePresent + 1);

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
