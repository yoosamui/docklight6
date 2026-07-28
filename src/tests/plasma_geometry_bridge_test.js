#!/usr/bin/env node

"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const packageId =
    "org.docklight6.geometrybridge";

class Widget {
    constructor(type, containment) {
        this.type = type;
        this.containment = containment;
        this.id = containment.nextWidgetId++;
    }

    remove() {
        this.containment.removeWidget(
            this.id);
    }
}

class Containment {
    constructor() {
        this.nextWidgetId = 1;
        this.widgets = new Map();
    }

    get widgetIds() {
        return Array.from(
            this.widgets.keys());
    }

    widgetById(widgetId) {
        return this.widgets.get(widgetId);
    }

    addWidget(type) {
        const widget =
            new Widget(type, this);

        this.widgets.set(
            widget.id,
            widget);

        return widget;
    }

    removeWidget(widgetId) {
        this.widgets.delete(widgetId);
    }
}

const scriptPath =
    path.join(
        __dirname,
        "../../plasma/geometry-bridge/"
            + "ensure-geometry-bridge.js");
const script =
    fs.readFileSync(
        scriptPath,
        "utf8");

function runBridgeScript(
    desktopContainments,
    panelContainments)
{
    vm.runInNewContext(
        script,
        {
            desktops: () =>
                desktopContainments,
            panels: () =>
                panelContainments
        });
}

function bridgeCount(containment) {
    return containment.widgetIds
        .map(widgetId =>
            containment.widgetById(
                widgetId))
        .filter(widget =>
            widget.type === packageId)
        .length;
}

function totalBridgeCount(
    containments)
{
    return containments.reduce(
        (total, containment) =>
            total +
            bridgeCount(containment),
        0);
}

{
    const panel =
        new Containment();

    runBridgeScript([], [panel]);
    assert.strictEqual(
        bridgeCount(panel),
        1);

    runBridgeScript([], [panel]);
    assert.strictEqual(
        bridgeCount(panel),
        1);
}

{
    const ordinaryPanel =
        new Containment();
    const taskManagerPanel =
        new Containment();

    taskManagerPanel.addWidget(
        "org.kde.plasma.icontasks");

    runBridgeScript(
        [],
        [
            ordinaryPanel,
            taskManagerPanel
        ]);

    assert.strictEqual(
        bridgeCount(ordinaryPanel),
        0);
    assert.strictEqual(
        bridgeCount(taskManagerPanel),
        1);
}

{
    const ordinaryPanel =
        new Containment();
    const taskManagerPanel =
        new Containment();

    taskManagerPanel.addWidget(
        "org.kde.plasma.taskmanager");
    ordinaryPanel.addWidget(packageId);
    taskManagerPanel.addWidget(packageId);

    runBridgeScript(
        [],
        [
            ordinaryPanel,
            taskManagerPanel
        ]);

    assert.strictEqual(
        bridgeCount(ordinaryPanel),
        0);
    assert.strictEqual(
        bridgeCount(taskManagerPanel),
        1);
}

{
    const desktop =
        new Containment();
    const panel =
        new Containment();

    desktop.addWidget(packageId);

    runBridgeScript(
        [desktop],
        [panel]);

    assert.strictEqual(
        totalBridgeCount(
            [desktop, panel]),
        1);
    assert.strictEqual(
        bridgeCount(desktop),
        0);
    assert.strictEqual(
        bridgeCount(panel),
        1);
}

assert.throws(
    () => runBridgeScript([], []),
    /No Plasma panel/);

console.log(
    "Plasma geometry bridge tests passed");
