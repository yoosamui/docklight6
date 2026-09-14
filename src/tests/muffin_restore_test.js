#!/usr/bin/env node

// ------------------------------------------------------------
// Docklight 6.0
//
// File: muffin_restore_test.js
// Exercises the actual Cinnamon activation script with window doubles.
// Actor enumeration must not replace the requested back-to-front order;
// missing windows must be skipped without changing the surviving target.
// ------------------------------------------------------------

"use strict";

const assert = require("assert");
const fs = require("fs");
const path = require("path");
const vm = require("vm");

const source = fs.readFileSync(path.resolve(
    __dirname, "../integrations/x11/muffin_window_backend.cpp"), "utf8");
const method = source.match(
    /MuffinWindowBackend::activate_windows_override\([\s\S]*?\n\}/)[0];
// Decode the C++ literal fragments, inserting IDs where the C++ loop does.
const prefix = JSON.parse(method.match(/std::string script =\s*("[^"\n]*")/)[1]);
const suffix = [...method.match(/script \+=\s*\n([\s\S]*?);\n/)[1]
    .matchAll(/"(?:\\.|[^"\\])*"/g)]
    .map(match => JSON.parse(match[0])).join("");

function restore(ids, actorIds) {
    const events = [];
    let stack = [...actorIds];
    const windows = actorIds.map(id => ({
        get_xwindow: () => id,
        unminimize: () => events.push(["restore", id]),
        raise() {
            stack = stack.filter(other => other !== id).concat(id);
            events.push(["raise", id]);
        },
        get_workspace: () => ({
            activate_with_focus(target, timestamp) {
                assert.strictEqual(target.get_xwindow(), id);
                assert.strictEqual(timestamp, 123);
                events.push(["focus", id]);
            }
        })
    }));
    const accepted = vm.runInNewContext(prefix + ids.join(",") + suffix, {
        global: {
            get_window_actors: () => windows.map(meta_window => ({meta_window})),
            get_current_time: () => 123
        }
    });
    return {accepted, events, stack};
}

for (const actors of [[1, 2, 3, 99], [3, 1, 99, 2], [99, 2, 3, 1]]) {
    for (const ids of [[2, 1], [1, 2], [3, 2, 1]]) {
        const result = restore(ids, actors);
        assert.strictEqual(result.accepted, true);
        assert.deepStrictEqual(result.stack.slice(-ids.length), ids);
        assert.deepStrictEqual(result.events, [
            ...ids.flatMap(id => [["restore", id], ["raise", id]]),
            ["focus", ids[ids.length - 1]]
        ]);
    }
}
assert.deepStrictEqual(restore([2, 404, 1], [1, 2]).stack, [2, 1]);
assert.deepStrictEqual(restore([2, 1, 404], [1, 2]).events.at(-1), ["focus", 1]);
assert.deepStrictEqual(restore([1], [2, 1]).events,
    [["restore", 1], ["raise", 1], ["focus", 1]]);
assert.strictEqual(restore([404], [1, 2]).accepted, false);
assert.deepStrictEqual(restore([404], [1, 2]).events, []);

console.log("Muffin restore ordering tests passed");
