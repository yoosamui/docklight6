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
dockWindow.windowRole = "docklight6-dock";
dockWindow.skipTaskbar = true;
dockWindow.x11Client = true;
dockWindow.output = {
    geometry: {
        x: 0,
        y: 0,
        width: 1920,
        height: 1080
    }
};

const secondOutput = {
    geometry: {
        x: 1920,
        y: 0,
        width: 1920,
        height: 1080
    }
};

const dockPopup =
    createWindow(
        "dock-popup",
        "org.docklight6");

dockPopup.resourceName = "docklight6";
dockPopup.windowRole = "docklight6-popup";
dockPopup.skipTaskbar = true;
dockPopup.popupWindow = true;

const replacementDockWindow =
    createWindow(
        "replacement-dock-window",
        "org.docklight6");

replacementDockWindow.resourceName =
    "docklight6";
replacementDockWindow.windowRole =
    "docklight6-dock";
replacementDockWindow.skipTaskbar = true;
replacementDockWindow.x11Client = true;
replacementDockWindow.frameGeometry = {
    x: 100,
    y: 700,
    width: 600,
    height: 64
};
replacementDockWindow.output =
    dockWindow.output;

const plasmaPanel =
    createWindow(
        "plasma-panel",
        "org.kde.plasmashell");

plasmaPanel.resourceName = "plasmashell";
plasmaPanel.dock = true;
plasmaPanel.frameGeometry = {
    x: 0,
    y: 0,
    width: 1920,
    height: 60
};

let clientAreaGeometry = {
    x: 0,
    y: 44,
    width: 1920,
    height: 1036
};

const workspace = {
    stackingOrder: [
        managedWindow,
        plasmaPanel,
        dockWindow
    ],
    currentDesktop: {
        id: "desktop,two",
        x11DesktopNumber: 1
    },
    currentActivity: "activity,two",
    cursorPos: {
        x: 400,
        y: 300
    },
    screens: [
        dockWindow.output
    ],
    clientArea(option, window) {
        assert.strictEqual(
            option,
            3);
        assert(
            window === dockWindow ||
            window === replacementDockWindow);

        return clientAreaGeometry;
    },
    windowAdded: new Signal(),
    windowRemoved: new Signal(),
    windowActivated: new Signal(),
    currentDesktopChanged:
        new Signal(),
    cursorPosChanged:
        new Signal(),
    screensChanged:
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
const screenEdgeCallbacks = new Map();

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

function registerScreenEdge(
    edge,
    callback) {
    screenEdgeCallbacks.set(
        edge,
        callback);
}

function unregisterScreenEdge(edge) {
    screenEdgeCallbacks.delete(edge);
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
    KWin: {
        MaximizeArea: 3,
        ElectricTop: 0,
        ElectricRight: 1,
        ElectricBottom: 2,
        ElectricLeft: 3
    },
    callDBus,
    print,
    registerScreenEdge,
    unregisterScreenEdge,
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
    ["10"]);

calls[0].callback(
    true,
    "10");

const dockPlacementRequest =
    calls.find(
        call =>
            call.methodName ===
            "GetDockPlacementGeometry");

assert(dockPlacementRequest);

dockPlacementRequest.callback(
    true,
    100,
    44,
    800,
    600);

assert.strictEqual(
    dockWindow.frameGeometry.x,
    100);
assert.strictEqual(
    dockWindow.frameGeometry.y,
    44);
assert.strictEqual(
    dockWindow.frameGeometry.width,
    800);
assert.strictEqual(
    dockWindow.frameGeometry.height,
    600);

dockWindow.frameGeometry = {
    x: 10,
    y: 20,
    width: 800,
    height: 600
};

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

const dockWorkAreaGeometry =
    calls.find(
        call =>
            call.methodName ===
            "PublishDockWorkAreaGeometry");

const initialDockPointerState =
    calls.find(
        call =>
            call.methodName ===
            "PublishDockPointerInside");

assert(beginSnapshot);
assert.strictEqual(
    stagedWindows.length,
    1);
assert(commitSnapshot);
assert(dockSurfaceGeometry);
assert(dockWorkAreaGeometry);
assert(initialDockPointerState);
assert.deepStrictEqual(
    initialDockPointerState.arguments,
    [true]);
assert.deepStrictEqual(
    dockSurfaceGeometry.arguments,
    [
        "2",
        10,
        20,
        800,
        600
    ]);
assert.deepStrictEqual(
    dockWorkAreaGeometry.arguments,
    [
        "3",
        0,
        44,
        1920,
        1036
    ]);

workspace.cursorPos = {
    x: 1800,
    y: 900
};
workspace.cursorPosChanged.emit();

const outsideDockPointerState =
    calls
        .filter(
            call =>
                call.methodName ===
                "PublishDockPointerInside")
        .at(-1);

assert.deepStrictEqual(
    outsideDockPointerState.arguments,
    [false]);

// A non-autohiding Docklight contributes its own far edge to MaximizeArea.
// Remove that self-reservation before rebuilding the Plasma-panel-only area,
// or each relayout would move the dock farther inward.
clientAreaGeometry = {
    x: 0,
    y: 620,
    width: 1920,
    height: 460
};
dockWindow.frameGeometryChanged.emit();

const workAreaWithoutOwnReservation =
    calls
        .filter(
            call =>
                call.methodName ===
                "PublishDockWorkAreaGeometry")
        .at(-1);

assert.deepStrictEqual(
    workAreaWithoutOwnReservation
        .arguments.slice(1),
    [
        0,
        44,
        1920,
        1036
    ]);

clientAreaGeometry = {
    x: 0,
    y: 44,
    width: 1920,
    height: 1036
};

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

// Plasma's own panel can cover Docklight's layer-shell trigger. Register the
// corresponding KWin screen edge so reveal activation remains compositor-
// owned and does not rewrite Docklight's ordinary hover state.
replacementDockWindow.frameGeometry = {
    x: 100,
    y: 44,
    width: 600,
    height: 64
};
replacementDockWindow
    .frameGeometryChanged
    .emit();

const topScreenEdgeCallback =
    screenEdgeCallbacks.get(
        context.KWin.ElectricTop);
const callsFor = methodName =>
    calls.filter(
        call =>
            call.methodName === methodName);

assert(topScreenEdgeCallback);
assert.strictEqual(
    screenEdgeCallbacks.has(
        context.KWin.ElectricBottom),
    false);

const hiddenQueriesBeforeEdge =
    callsFor("GetDockHidden").length;
const revealRequestsBeforeEdge =
    callsFor("RequestDockReveal").length;
const pointerPublishesBeforeReveal =
    callsFor("PublishDockPointerInside")
        .length;

workspace.cursorPos = {
    x: 400,
    y: 0
};

topScreenEdgeCallback();

const hiddenQueryAtEdge =
    callsFor("GetDockHidden").at(-1);

assert.strictEqual(
    callsFor("GetDockHidden").length,
    hiddenQueriesBeforeEdge + 1);

hiddenQueryAtEdge.callback(true);

const revealAtCoveredEdge =
    callsFor("RequestDockReveal").at(-1);

assert.strictEqual(
    callsFor("RequestDockReveal").length,
    revealRequestsBeforeEdge + 1);

revealAtCoveredEdge.callback(true);

assert.strictEqual(
    callsFor("PublishDockPointerInside")
        .length,
    pointerPublishesBeforeReveal);

workspace.cursorPos = {
    x: 800,
    y: 0
};
topScreenEdgeCallback();

assert.strictEqual(
    callsFor("GetDockHidden").length,
    hiddenQueriesBeforeEdge + 1);

workspace.cursorPos = {
    x: 400,
    y: 0
};
topScreenEdgeCallback();

const visibleDockQuery =
    callsFor("GetDockHidden").at(-1);

visibleDockQuery.callback(false);

assert.strictEqual(
    callsFor("RequestDockReveal").length,
    revealRequestsBeforeEdge + 1);

// KWin's ElectricRight edge is the outer edge of the whole desktop, not an
// internal border. Reject its far-monitor callback and reveal when the cursor
// actually crosses the dock output's shared boundary instead.
workspace.screens.push(secondOutput);
workspace.screensChanged.emit();
replacementDockWindow.frameGeometry = {
    x: 1856,
    y: 200,
    width: 64,
    height: 600
};
replacementDockWindow
    .frameGeometryChanged
    .emit();

const rightScreenEdgeCallback =
    screenEdgeCallbacks.get(
        context.KWin.ElectricRight);
const hiddenQueriesBeforeFarEdge =
    callsFor("GetDockHidden").length;

assert(rightScreenEdgeCallback);

workspace.cursorPos = {
    x: 3839,
    y: 400
};
rightScreenEdgeCallback();

assert.strictEqual(
    callsFor("GetDockHidden").length,
    hiddenQueriesBeforeFarEdge);

workspace.cursorPos = {
    x: 1919,
    y: 400
};
workspace.cursorPosChanged.emit();
workspace.cursorPos = {
    x: 1920,
    y: 400
};
workspace.cursorPosChanged.emit();

const internalBoundaryQuery =
    callsFor("GetDockHidden").at(-1);

assert.strictEqual(
    callsFor("GetDockHidden").length,
    hiddenQueriesBeforeFarEdge + 1);

internalBoundaryQuery.callback(true);

const internalBoundaryReveal =
    callsFor("RequestDockReveal").at(-1);

assert.strictEqual(
    callsFor("RequestDockReveal").length,
    revealRequestsBeforeEdge + 2);

internalBoundaryReveal.callback(true);

// KWin may push the cursor one pixel inward while delivering an outer screen
// edge callback. Accept that coordinate on the dock's own output.
replacementDockWindow.frameGeometry = {
    x: 0,
    y: 200,
    width: 64,
    height: 600
};
replacementDockWindow
    .frameGeometryChanged
    .emit();

const leftScreenEdgeCallback =
    screenEdgeCallbacks.get(
        context.KWin.ElectricLeft);
const hiddenQueriesBeforeLeftEdge =
    callsFor("GetDockHidden").length;

assert(leftScreenEdgeCallback);

workspace.cursorPos = {
    x: 1,
    y: 400
};
leftScreenEdgeCallback();

assert.strictEqual(
    callsFor("GetDockHidden").length,
    hiddenQueriesBeforeLeftEdge + 1);

callsFor("GetDockHidden")
    .at(-1)
    .callback(false);

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
