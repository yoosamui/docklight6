/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

"use strict";

const docklightMinimizeEffect = {
    duration: animationTime(320),
    geometries: {},

    loadConfig: function () {
        const configuredDuration =
            effect.readConfig(
                "AnimationDuration",
                320);

        docklightMinimizeEffect.duration =
            animationTime(
                configuredDuration > 0
                    ? configuredDuration
                    : 320);

        docklightMinimizeEffect.geometries = {};

        const entries =
            String(
                effect.readConfig(
                    "Geometries",
                    ""))
                .split(";");

        for (const entry of entries) {
            if (!entry)
                continue;

            const fields = entry.split(",");

            if (fields.length !== 5)
                continue;

            const processId = Number(fields[0]);
            const geometry = {
                x: Number(fields[1]),
                y: Number(fields[2]),
                width: Number(fields[3]),
                height: Number(fields[4])
            };

            if (!Number.isFinite(processId) ||
                processId <= 0 ||
                !Number.isFinite(geometry.x) ||
                !Number.isFinite(geometry.y) ||
                !Number.isFinite(geometry.width) ||
                !Number.isFinite(geometry.height) ||
                geometry.width <= 0 ||
                geometry.height <= 0) {
                continue;
            }

            docklightMinimizeEffect
                .geometries[String(processId)] =
                geometry;
        }
    },

    iconGeometry: function (window) {
        const docklightGeometry =
            docklightMinimizeEffect
                .geometries[
                    String(window.pid)];

        if (docklightGeometry)
            return docklightGeometry;

        return window.iconGeometry;
    },

    animateWindow: function (
        window,
        minimizing) {
        if (effects.hasActiveFullScreenEffect)
            return;

        const iconRect =
            docklightMinimizeEffect
                .iconGeometry(window);

        if (!iconRect ||
            iconRect.width <= 0 ||
            iconRect.height <= 0) {
            return;
        }

        const reverseAnimation =
            minimizing
                ? window.docklightUnminimizeAnimation
                : window.docklightMinimizeAnimation;

        if (reverseAnimation) {
            if (redirect(
                    reverseAnimation,
                    minimizing
                        ? Effect.Backward
                        : Effect.Forward)) {
                return;
            }

            cancel(reverseAnimation);
        }

        const currentAnimation =
            minimizing
                ? window.docklightMinimizeAnimation
                : window.docklightUnminimizeAnimation;

        if (currentAnimation)
            cancel(currentAnimation);

        const windowRect = window.geometry;
        const translation = {
            value1:
                iconRect.x -
                windowRect.x -
                (windowRect.width -
                 iconRect.width) / 2,
            value2:
                iconRect.y -
                windowRect.y -
                (windowRect.height -
                 iconRect.height) / 2
        };

        const animation = animate({
            window: window,
            curve:
                minimizing
                    ? QEasingCurve.InCubic
                    : QEasingCurve.OutCubic,
            duration:
                docklightMinimizeEffect.duration,
            animations: [
                {
                    type: Effect.Size,
                    from:
                        minimizing
                            ? {
                                  value1:
                                      windowRect.width,
                                  value2:
                                      windowRect.height
                              }
                            : {
                                  value1:
                                      iconRect.width,
                                  value2:
                                      iconRect.height
                              },
                    to:
                        minimizing
                            ? {
                                  value1:
                                      iconRect.width,
                                  value2:
                                      iconRect.height
                              }
                            : {
                                  value1:
                                      windowRect.width,
                                  value2:
                                      windowRect.height
                              }
                },
                {
                    type: Effect.Translation,
                    from:
                        minimizing
                            ? {
                                  value1: 0,
                                  value2: 0
                              }
                            : translation,
                    to:
                        minimizing
                            ? translation
                            : {
                                  value1: 0,
                                  value2: 0
                              }
                },
                {
                    type: Effect.Opacity,
                    from: minimizing ? 1 : 0,
                    to: minimizing ? 0 : 1
                }
            ]
        });

        if (minimizing) {
            window.docklightMinimizeAnimation =
                animation;
            delete window
                .docklightUnminimizeAnimation;
        } else {
            window.docklightUnminimizeAnimation =
                animation;
            delete window
                .docklightMinimizeAnimation;
        }
    },

    manageWindow: function (window) {
        window.minimizedChanged.connect(
            function () {
                docklightMinimizeEffect
                    .animateWindow(
                        window,
                        window.minimized);
            });
    },

    init: function () {
        effect.configChanged.connect(
            docklightMinimizeEffect.loadConfig);

        effects.windowAdded.connect(
            docklightMinimizeEffect.manageWindow);

        for (const window of
             effects.stackingOrder) {
            docklightMinimizeEffect
                .manageWindow(window);
        }

        docklightMinimizeEffect.loadConfig();
    }
};

docklightMinimizeEffect.init();
