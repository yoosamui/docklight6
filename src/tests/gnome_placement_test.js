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
const autohideControllerPath = path.resolve(
    __dirname,
    "../autohide/dock_autohide_controller.cpp");
const dockWindowControllerPath = path.resolve(
    __dirname,
    "../dock/dock_window_controller.cpp");
const previewWindowPath = path.resolve(
    __dirname,
    "../preview/dock_preview_window.cpp");
const thumbnailProviderPath = path.resolve(
    __dirname,
    "../preview/dock_window_thumbnail_provider.cpp");

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
const autohideControllerSource = fs.readFileSync(
    autohideControllerPath, "utf8");
const dockWindowControllerSource = fs.readFileSync(
    dockWindowControllerPath, "utf8");
const previewWindowSource = fs.readFileSync(
    previewWindowPath, "utf8");
const thumbnailProviderSource = fs.readFileSync(
    thumbnailProviderPath, "utf8");

assert.match(
    extensionSource,
    /<method name="ShowLivePreviews">[\s\S]*?a\(siiii\)[\s\S]*?<method name="HideLivePreviews"/,
    "the GNOME thumbnail service must expose the compositor overlay lifecycle");
assert.match(
    extensionSource,
    /ShowLivePreviewsAsync[\s\S]*?new Shell\.WindowPreviewLayout\(\)[\s\S]*?layout\.add_window\(window\)/,
    "premium GNOME previews must clone live compositor actors instead of polling screenshots");
assert.match(
    extensionSource,
    /_isApplicationAuxiliary\(window\)[\s\S]*?skip_taskbar[\s\S]*?is_above[\s\S]*?sameApplication[\s\S]*?_windowPayload\(window\)[\s\S]*?_isApplicationAuxiliary\(window\)/,
    "always-on-top application auxiliaries such as Firefox PiP must remain in preview groups");
assert.match(
    extensionSource,
    /const overlay = new Clutter\.Actor\(\{[\s\S]*?reactive: false[\s\S]*?const preview = new Clutter\.Actor\(\{[\s\S]*?reactive: true[\s\S]*?Main\.uiGroup\.add_child\(overlay\)/,
    "only the thumbnail-sized preview actors may accept Shell input");
assert.match(
    extensionSource,
    /const disableDescendantInput = actor => \{[\s\S]*?actor\.get_children\(\)[\s\S]*?child\.reactive = false[\s\S]*?disableDescendantInput\(preview\)/,
    "compositor clone descendants must leave input handling to their preview container");
assert.doesNotMatch(
    extensionSource,
    /overlay\.connect\('captured-event'/,
    "the Shell overlay must not capture GTK preview-card input");
assert.match(
    extensionSource,
    /_pointerPosition = \{x: position\.x, y: position\.y\};[\s\S]*?_publishPreviewPointerInside\(\)[\s\S]*?_previewPointerIsInside\(\)[\s\S]*?_livePreviewRects\.some/,
    "live preview hover must be reconciled from the compositor pointer instead of relying only on crossing events");
assert.match(
    extensionSource,
    /const selector = new St\.Widget\(\{[\s\S]*?reactive: false[\s\S]*?overlay\.add_child\(selector\)[\s\S]*?selector,/,
    "live compositor previews must paint their selector from authoritative pointer motion without intercepting clicks");
assert.match(
    extensionSource,
    /_updateLivePreviewSelectors\(\)[\s\S]*?selected === rect\.selected[\s\S]*?rect\.selector\.opacity = selected \? 255 : 0[\s\S]*?_publishPreviewPointerInside\([^)]*\) \{[\s\S]*?_updateLivePreviewSelectors\(\)/,
    "live preview selectors must follow the compositor pointer even while it remains inside the preview surface");
assert.match(
    extensionSource,
    /_destroyLivePreviews\(false\)[\s\S]*?const previewRects = \[\][\s\S]*?this\._livePreviewRects = previewRects[\s\S]*?_publishPreviewPointerInside\(true\)/,
    "replacing live previews must atomically install and publish their pointer hitboxes");
assert.match(
    extensionSource,
    /const previewActors = \[\][\s\S]*?previewActors\.push\(preview\)[\s\S]*?for \(const preview of previewActors\)[\s\S]*?preview\.ease/,
    "only thumbnail-sized actors may be animated when live previews appear");
assert.match(
    extensionSource,
    /Main\.uiGroup\.add_child\(overlay\)[\s\S]*?for \(const preview of previewActors\)[\s\S]*?Main\.layoutManager\.trackChrome\(preview, \{[\s\S]*?affectsInputRegion: true/,
    "each live thumbnail must be included in Shell's stage input region");
assert.match(
    extensionSource,
    /_destroyLivePreviews\([^)]*\) \{[\s\S]*?for \(const preview of this\._livePreviewActors\)[\s\S]*?Main\.layoutManager\.untrackChrome\(preview\)/,
    "destroying live previews must remove their Shell input-region tracking");
assert.doesNotMatch(
    extensionSource,
    /overlay\.ease\(/,
    "the full-stage live-preview container must not be opacity animated");
assert.match(
    extensionSource,
    /preview\.connect\('button-press-event'[\s\S]*?primaryButtonPressed = true[\s\S]*?Clutter\.EVENT_STOP[\s\S]*?preview\.connect\('button-release-event'[\s\S]*?!primaryButtonPressed[\s\S]*?ActivatePreviewWindow[\s\S]*?Clutter\.EVENT_STOP/,
    "a compositor preview must consume a complete primary click before activating its GTK preview card action through the integration service");
assert.match(
    extensionSource,
    /_isApplicationAuxiliary\(window\)[\s\S]*?_forwardPreviewPrimaryClick\(window, preview, event\)[\s\S]*?_forwardPreviewPrimaryClick\(window, preview, event\) \{[\s\S]*?get_frame_rect[\s\S]*?get_coords[\s\S]*?get_transformed_position[\s\S]*?get_transformed_size/,
    "PiP preview clicks must map clone coordinates back to the real client frame");
assert.match(
    extensionSource,
    /create_virtual_device\([\s\S]*?Clutter\.InputDeviceType\.TOUCHSCREEN_DEVICE[\s\S]*?notify_touch_down\([\s\S]*?notify_touch_up\(/,
    "PiP preview clicks must reach the real Wayland client through compositor virtual touch input");
assert.doesNotMatch(
    extensionSource,
    /hostActor\.add_child\(overlay\)/,
    "live-preview actors must not be parented into a real window actor");
assert.match(
    extensionSource,
    /disable\(\)[\s\S]*?_destroyLivePreviews\(\)[\s\S]*?_destroyLivePreviews\([^)]*\) \{[\s\S]*?\.destroy\(\)/,
    "disabling the extension must destroy every compositor preview clone");
assert.match(
    extensionSource,
    /_disconnectBackend\(\) \{[\s\S]*?_destroyLivePreviews\(\)/,
    "losing the Docklight service must destroy every compositor preview clone");
assert.match(
    extensionSource,
    /_isThumbnailCallerAuthorized\(invocation\)[\s\S]*?get_name_owner[\s\S]*?get_sender[\s\S]*?CaptureWindowAsync[\s\S]*?_isThumbnailCallerAuthorized\(invocation\)[\s\S]*?ShowLivePreviewsAsync[\s\S]*?_isThumbnailCallerAuthorized\(invocation\)/,
    "only the registered Docklight service owner may access compositor window textures");
assert.match(
    thumbnailProviderSource,
    /ShowLivePreviews[\s\S]*?g_variant_new\("\(a\(siiii\)\)"/,
    "Docklight must publish live preview rectangles through the GNOME service");
assert.match(
    previewWindowSource,
    /supports_gnome_live_previews\(\)[\s\S]*?global_x \+ m_size\.padding[\s\S]*?global_y \+ WINDOW_PADDING[\s\S]*?show_gnome_live_previews/,
    "GNOME compositor actors must align with the existing GTK card bodies");
assert.match(
    dockWindowControllerSource,
    /set_dock_placement_geometry[\s\S]*?calculated_dock_screen_position|calculated_dock_screen_position[\s\S]*?set_dock_placement_geometry/,
    "XWayland monitor changes must publish calculated target geometry, not a stale mapped origin");
assert.match(
    dockWindowControllerSource,
    /GDK_IS_X11_DISPLAY[\s\S]*?capture_x11_base_workarea[\s\S]*?m_x11_base_workarea/,
    "X11 layout must not feed Docklight's own strut back into its edge margin");
assert.match(
    extensionSource,
    /_isX11DockWindow\(window\)[\s\S]*?_removeDockStrut\(\)[\s\S]*?_publishDockSurfaceGeometry\(rect\)/,
    "GNOME must leave XWayland dock placement and reservation to EWMH");
assert.match(
    autohideControllerSource,
    /hide_now\([\s\S]*?\)[\s\S]*?if \(uses_shell_reveal_trigger\(\)\)[\s\S]*?request_shell_visibility\(true\);\s*return;/,
    "GNOME autohide must keep the placed dock mapped instead of remapping at the centre");
assert.match(
    autohideControllerSource,
    /finish_shell_animation\([\s\S]*?hidden[\s\S]*?ShellDockState::hidden[\s\S]*?set_shell_input_passthrough\(true\)/,
    "input pass-through must begin only after Shell completes the hide animation");
assert.match(
    extensionSource,
    /_startDockVisibilityTransition\(hidden[\s\S]*?actor\.ease\([\s\S]*?EASE_IN_QUAD[\s\S]*?EASE_OUT_QUAD/,
    "GNOME must use a native compositor transition at the screen edge");
assert.match(
    extensionSource,
    /remainingDistance < 0\.5[\s\S]*?completeTransition\(\)[\s\S]*?return;[\s\S]*?actor\.ease/,
    "an already-positioned actor must still complete the visibility state change");
assert.match(
    extensionSource,
    /completionSource = GLib\.timeout_add\([\s\S]*?duration \+ 32[\s\S]*?completeTransition\(\)[\s\S]*?actor\.ease/,
    "a compositor callback omission must not strand the visibility state machine");
assert.match(
    extensionSource,
    /_finishDockTransition\(\)[\s\S]*?_startDockVisibilityTransition\(\s*this\._dockHidden, this\._dockActor\)/,
    "placement completion must not reveal a dock that autohid while placement settled");
assert.match(
    autohideControllerSource,
    /set_shell_pointer_inside\([\s\S]*?m_pointer_inside = inside[\s\S]*?ShellDockState::hiding[\s\S]*?reveal\(\)/,
    "authoritative Shell pointer entry must reverse an in-progress hide");
assert.match(
    autohideControllerSource,
    /reveal\(\)[\s\S]*?if \(uses_shell_reveal_trigger\(\)\)[\s\S]*?request_shell_visibility\(false\);/,
    "a Shell reveal must restore the existing mapped dock surface");
assert.match(
    extensionSource,
    /signalName === 'DockHiddenChanged'[\s\S]*?this\._dockHidden = Boolean[\s\S]*?this\._startDockVisibilityTransition/,
    "Shell must drive its reveal strip from the application's hidden state");
assert.match(
    extensionSource,
    /global\.backend\.get_cursor_tracker\(\)[\s\S]*?_cursorTracker\.get_pointer\(\)[\s\S]*?_pointerPosition[\s\S]*?_publishDockPointerInside[\s\S]*?PublishDockPointerInside/,
    "Shell must publish compositor-global pointer presence instead of relying on GTK crossing events");
assert.doesNotMatch(
    extensionSource,
    /global\.get_pointer/,
    "pointer tracking must not use the removed GNOME Shell global pointer API");
assert.doesNotMatch(
    autohideControllerSource,
    /animate_shell_opacity/,
    "GNOME visibility must have only one animation owner");
assert.match(
    extensionSource,
    /_placeAuxiliaryWindow[\s\S]*?actor\.translation_x = resolvedTarget\.x - rect\.x[\s\S]*?actor\.translation_y = resolvedTarget\.y - rect\.y/,
    "private surfaces must use compositor translation instead of unsafe Mutter moves");
assert.match(
    extensionSource,
    /target\.type === 'reveal'[\s\S]*?clampAuxiliaryToWorkArea\([\s\S]*?_workAreaForMonitor/,
    "GNOME previews and tooltips must clamp to the Shell work area without moving the reveal strip");
assert.match(
    dockWindowControllerSource,
    /show_tooltip\([\s\S]*?dock_screen_position\(true\)[\s\S]*?show_tooltip\(/,
    "GNOME tooltips must follow the compositor-confirmed dock surface origin");
assert.match(
    dockWindowControllerSource,
    /show_preview\([\s\S]*?dock_screen_position\(true\)[\s\S]*?show_preview\(/,
    "GNOME previews must centre on the compositor-confirmed dock surface origin");
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
    /if \(this\._isAuxiliaryWindow\(window\)\)\s*this\._beginAuxiliaryTransition\(window, actor\)/,
    "ordinary Wayland auxiliary actors must be hidden during their first map");
assert.match(
    extensionSource,
    /_beginAuxiliaryTransition\(window, actor = null\)[\s\S]*?compositorActor\.set_opacity\(0\)/,
    "a provisional centred auxiliary frame must not enter the scene");
assert.match(
    extensionSource,
    /_beginDockTransition\(\)[\s\S]*?actor\.remove_all_transitions\(\)[\s\S]*?actor\.scale_x = 1[\s\S]*?actor\.scale_y = 1[\s\S]*?actor\.set_opacity\(0\)/,
    "GNOME's normal-window map animation must not expose the centred dock actor");
assert.match(
    extensionSource,
    /_disconnectBackend\(\) \{[\s\S]*?this\._dockDiscoveredOnce = false/,
    "an app-only restart must rearm early GNOME dock discovery");
assert.match(
    extensionSource,
    /_scheduleDockDiscoveryScan\(\)[\s\S]*?global\.get_window_actors\(\)[\s\S]*?_considerDockWindow\([\s\S]*?false\)/,
    "registration must rescan mapped actors until delayed dock metadata is available");
assert.match(
    extensionSource,
    /_placeDialogWindow\(window\) \{[\s\S]*?const actor = window\?\.get_compositor_private\?\.\(\)[\s\S]*?actor\.translation_x = x - rect\.x[\s\S]*?actor\.translation_y = y - rect\.y/,
    "GNOME dialogs must use safe compositor placement after mapping");
assert.match(
    extensionSource,
    /_placeAuxiliaryWindow[\s\S]*?actor\.translation_y = resolvedTarget\.y - rect\.y;[\s\S]*?this\._finishAuxiliaryTransition\(window\)/,
    "auxiliary opacity must be restored after compositor placement");
assert.match(
    extensionSource,
    /_placeAuxiliaryWindow[\s\S]*?window\.raise\(\)[\s\S]*?_finishAuxiliaryTransition\(window\)/,
    "GNOME must preserve layer-shell overlay ordering through Mutter's window stack");
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
assert.doesNotMatch(
    auxiliaryPlacementSource,
    /window\.move_frame/,
    "short-lived private Wayland surfaces must never call Meta.Window.move_frame");
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
    /this\._dockAutohide === 'none'[\s\S]*?this\._dockVisibilityState !== 'hidden'[\s\S]*?this\._call\('RequestDockReveal'/,
    "the Shell edge strip must request reveal only while the persistent dock is hidden");
assert.match(
    extensionSource,
    /const committed = isDockPlacementCommitted\([\s\S]*?if \(committed\)[\s\S]*?actor\.translation_x = actorOffset\.x[\s\S]*?actor\.translation_y = actorOffset\.y[\s\S]*?actor\.translation_x = x - rect\.x[\s\S]*?actor\.translation_y = y - rect\.y/,
    "an asynchronously remapped dock must be painted at its edge, never at Mutter's provisional centre");
assert.match(
    extensionSource,
    /calculateDockRevealRect\(placement\)/,
    "the reveal strip must follow the dock's resolved work-area edge");

const source = fs.readFileSync(helperPath, "utf8")
    .replaceAll("export function ", "function ") +
    "\nthis.testApi = {calculateDockRevealRect, calculateDockStrut, " +
    "clampAuxiliaryToWorkArea, inferDockEdge, " +
    "isDockPlacementCommitted, isPointerInsideDockInterior, " +
    "isSyntheticApplicationId, " +
    "parseAuxiliaryPosition, placeDockInWorkArea};";
const context = {};
vm.createContext(context);
vm.runInContext(source, context, {filename: helperPath});

const {
    calculateDockRevealRect,
    calculateDockStrut,
    clampAuxiliaryToWorkArea,
    inferDockEdge,
    isDockPlacementCommitted,
    isPointerInsideDockInterior,
    isSyntheticApplicationId,
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
    {type: "reveal", x: 1168, y: 340});
assert.deepStrictEqual(
    {...parseAuxiliaryPosition("Docklight 6 Reveal@-2,-64")},
    {type: "reveal", x: -2, y: -64});
assert.strictEqual(parseAuxiliaryPosition("Docklight 6 Dock"), null);

assert.deepStrictEqual(
    {...clampAuxiliaryToWorkArea(
        {x: 76, y: 8},
        {width: 512, height: 512},
        {x: 0, y: 32, width: 1200, height: 1048})},
    {x: 76, y: 40});
assert.deepStrictEqual(
    {...clampAuxiliaryToWorkArea(
        {x: 76, y: 75},
        {width: 512, height: 512},
        {x: 0, y: 32, width: 1200, height: 1048})},
    {x: 76, y: 75});

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
    {x: 1106, y: 356, width: 64, height: 400, edge: "right"});

assert.deepStrictEqual(
    {...placeDockInWorkArea(
        primary,
        primaryWorkArea,
        {x: 0, y: 61, width: 62, height: 958},
        "center")},
    {x: 0, y: 77, width: 62, height: 958, edge: "left"});
assert.deepStrictEqual(
    {...placeDockInWorkArea(
        primary,
        primaryWorkArea,
        {x: 0, y: 61, width: 62, height: 958},
        "start")},
    {x: 0, y: 32, width: 62, height: 958, edge: "left"});
assert.deepStrictEqual(
    {...placeDockInWorkArea(
        primary,
        primaryWorkArea,
        {x: 0, y: 61, width: 62, height: 958},
        "end")},
    {x: 0, y: 122, width: 62, height: 958, edge: "left"});

const rightDockWorkArea = {x: 0, y: 32, width: 1106, height: 1048};
assert.deepStrictEqual(
    {...placeDockInWorkArea(
        primary,
        rightDockWorkArea,
        {x: 1106, y: 340, width: 64, height: 400})},
    {x: 1042, y: 356, width: 64, height: 400, edge: "right"});
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

const pointerFixtures = [
    {
        placement: {x: 300, y: 32, width: 400, height: 64, edge: "top"},
        edge: [500, 32],
        interior: [500, 40],
    },
    {
        placement: {x: 300, y: 1016, width: 400, height: 64, edge: "bottom"},
        edge: [500, 1079],
        interior: [500, 1070],
    },
    {
        placement: {x: 0, y: 340, width: 64, height: 400, edge: "left"},
        edge: [0, 500],
        interior: [8, 500],
    },
    {
        placement: {x: 1106, y: 340, width: 64, height: 400, edge: "right"},
        edge: [1169, 500],
        interior: [1160, 500],
    },
];
for (const fixture of pointerFixtures) {
    assert.strictEqual(isPointerInsideDockInterior(
        fixture.placement, ...fixture.edge), true);
    assert.strictEqual(isPointerInsideDockInterior(
        fixture.placement, ...fixture.interior), true);
}
assert.strictEqual(isPointerInsideDockInterior(
    pointerFixtures[1].placement, 299, 1070), false);

assert.strictEqual(isSyntheticApplicationId("window:218"), true);
assert.strictEqual(isSyntheticApplicationId(" WINDOW:42 "), true);
assert.strictEqual(isSyntheticApplicationId("window:editor"), false);
assert.strictEqual(isSyntheticApplicationId("org.example.Editor"), false);

assert.match(
    extensionSource,
    /_applicationId\(window\)[\s\S]*?isSyntheticApplicationId\(trackedId\)[\s\S]*?get_gtk_application_id\(\)[\s\S]*?get_wm_class_instance\(\)[\s\S]*?return ''/,
    "GNOME must ignore Shell window IDs when no persistent identity exists");
assert.match(
    extensionSource,
    /_enforceDockWindowLayer\(\)[\s\S]*?make_above\(\)[\s\S]*?stick\(\)/,
    "GNOME must keep the dock above ordinary windows on every workspace");

assert.deepStrictEqual(
    {...calculateDockRevealRect(pointerFixtures[0].placement)},
    {x: 300, y: 32, width: 400, height: 6});
assert.deepStrictEqual(
    {...calculateDockRevealRect(pointerFixtures[1].placement)},
    {x: 300, y: 1074, width: 400, height: 6});
assert.deepStrictEqual(
    {...calculateDockRevealRect(pointerFixtures[2].placement)},
    {x: 0, y: 340, width: 6, height: 400});
assert.deepStrictEqual(
    {...calculateDockRevealRect(pointerFixtures[3].placement)},
    {x: 1164, y: 340, width: 6, height: 400});

console.log("GNOME placement tests passed");
