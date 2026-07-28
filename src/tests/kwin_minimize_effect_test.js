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

function createWindow(processId) {
    return {
        pid: processId,
        minimized: false,
        geometry: {
            x: 10,
            y: 20,
            width: 800,
            height: 600
        },
        iconGeometry: {
            x: 900,
            y: 1000,
            width: 50,
            height: 50
        },
        minimizedChanged: new Signal()
    };
}

function plain(value) {
    return JSON.parse(
        JSON.stringify(value));
}

const docklightWindow =
    createWindow(1234);
const fallbackWindow =
    createWindow(5678);

const animations = [];

const effects = {
    hasActiveFullScreenEffect: false,
    stackingOrder: [
        docklightWindow,
        fallbackWindow
    ],
    windowAdded: new Signal()
};

const effect = {
    configChanged: new Signal(),
    readConfig(key, fallback) {
        if (key === "Geometries")
            return "1234,100,200,46,46";

        if (key === "AnimationDuration")
            return 320;

        return fallback;
    }
};

const context = {
    Number,
    String,
    QEasingCurve: {
        InCubic: "in-cubic",
        OutCubic: "out-cubic"
    },
    Effect: {
        Backward: "backward",
        Forward: "forward",
        Opacity: "opacity",
        Size: "size",
        Translation: "translation"
    },
    animationTime: value => value,
    animate: settings => {
        animations.push(settings);
        return animations.length;
    },
    cancel: () => {},
    redirect: () => false,
    effect,
    effects
};

const scriptPath =
    path.resolve(
        __dirname,
        "../../kwin/" +
            "org.docklight6.minimize/" +
            "contents/code/main.js");

vm.createContext(context);
vm.runInContext(
    fs.readFileSync(
        scriptPath,
        "utf8"),
    context,
    {
        filename: scriptPath
    });

docklightWindow.minimized = true;
docklightWindow.minimizedChanged.emit();

assert.strictEqual(
    animations.length,
    1);

const docklightAnimations =
    animations[0].animations;
const docklightSize =
    docklightAnimations.find(
        animation =>
            animation.type ===
            context.Effect.Size);
const docklightTranslation =
    docklightAnimations.find(
        animation =>
            animation.type ===
            context.Effect.Translation);

assert.deepStrictEqual(
    plain(docklightSize.to),
    {
        value1: 46,
        value2: 46
    });
assert.deepStrictEqual(
    plain(docklightTranslation.to),
    {
        value1: -287,
        value2: -97
    });

fallbackWindow.minimized = true;
fallbackWindow.minimizedChanged.emit();

assert.strictEqual(
    animations.length,
    2);

const fallbackTranslation =
    animations[1].animations.find(
        animation =>
            animation.type ===
            context.Effect.Translation);

assert.deepStrictEqual(
    plain(fallbackTranslation.to),
    {
        value1: 515,
        value2: 705
    });

docklightWindow.minimized = false;
docklightWindow.minimizedChanged.emit();

assert.strictEqual(
    animations.length,
    3);
assert.deepStrictEqual(
    plain(
        animations[2].animations.find(
            animation =>
                animation.type ===
                context.Effect.Translation)
            .from),
    {
        value1: -287,
        value2: -97
    });

console.log(
    "KWin minimize effect tests passed");
