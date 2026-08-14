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
    /signalName === 'IconGeometryChanged'[\s\S]*?_setIconGeometry\(\.\.\.parameters\.deepUnpack\(\)\)[\s\S]*?signalName === 'IconGeometryRemoved'[\s\S]*?_removeIconGeometry/,
    "GNOME must consume Docklight icon-geometry updates and removals");
assert.match(
    extensionSource,
    /GetIconGeometries[\s\S]*?reply\?\.\[0\][\s\S]*?_setIconGeometry\(\.\.\.geometry\)/,
    "GNOME must restore cached icon geometry after either side reconnects");
assert.match(
    extensionSource,
    /command === 'present'[\s\S]*?Main\.activateWindow\(\s*windows\.at\(-1\), global\.get_current_time\(\)\)[\s\S]*?command === 'activate'[\s\S]*?Main\.activateWindow\(window, global\.get_current_time\(\)\)/,
    "GNOME window activation must switch to an off-workspace target");
assert.match(
    extensionSource,
    /_setIconGeometry\(windowId, x, y, width, height\)[\s\S]*?get_frame_rect\(\)[\s\S]*?Object\.assign\(rect, geometry\)[\s\S]*?set_icon_geometry\(rect\)[\s\S]*?_removeIconGeometry\(windowId\)[\s\S]*?set_icon_geometry\(null\)/,
    "GNOME must register and unregister each DockItem rectangle with Mutter");
assert.match(
    extensionSource,
    /_disconnectBackend\(\) \{[\s\S]*?_clearIconGeometries\(\)/,
    "GNOME must not retain stale icon geometry after Docklight exits");
assert.match(
    dockWindowControllerSource,
    /signal_connection_changed\(\)[\s\S]*?if \(connected\)[\s\S]*?schedule_icon_geometry_update\(\)/,
    "DockItem geometry must be republished after the Shell backend connects");

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
    /const overlay = new Clutter\.Actor\(\{[\s\S]*?reactive: false[\s\S]*?const preview = new Clutter\.Actor\(\{[\s\S]*?reactive: false[\s\S]*?Main\.uiGroup\.add_child\(overlay\)/,
    "the compositor overlay and thumbnail actors must remain paint-only");
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
    /const selector = new St\.Widget\(\{[\s\S]*?reactive: false[\s\S]*?const selectionOutline = new St\.Widget\(\{[\s\S]*?reactive: false[\s\S]*?selector\.add_child\(preview\)[\s\S]*?selector\.add_child\(selectionOutline\)[\s\S]*?set_child_above_sibling\(selectionOutline, preview\)[\s\S]*?overlay\.add_child\(selector\)[\s\S]*?selector,[\s\S]*?selectionOutline,/,
    "live compositor previews must use a transparent paint-only selector and outline");
assert.doesNotMatch(
    extensionSource,
    /thumbnailBacking/,
    "the Shell overlay must not hide GTK's cached thumbnail fallback");
assert.match(
    extensionSource,
    /_updateLivePreviewSelectors\([^)]*\)[\s\S]*?selected === rect\.selected[\s\S]*?rect\.selector\.set_style\(selected[\s\S]*?rect\.selectionOutline\.set_style\(selected[\s\S]*?_publishPreviewPointerInside\([^)]*\) \{[\s\S]*?_updateLivePreviewSelectors\(\)/,
    "live preview selectors must follow the compositor pointer even while it remains inside the preview surface");
assert.match(
    extensionSource,
    /<method name="ShowLivePreviews">\s*<arg type="a\(siiii\)"[^>]*\/>\s*<\/method>[\s\S]*?<method name="SetPreviewColor">[\s\S]*?SetPreviewColorAsync[\s\S]*?_isThumbnailCallerAuthorized\(invocation\)[\s\S]*?_previewSelectorFill[\s\S]*?_previewSelectorOutline/,
    "preview color must use a separate authorized method without changing the established live-preview signature");
assert.doesNotMatch(
    extensionSource,
    /overlay\.add_child\(preview\)/,
    "live thumbnails must not be selector siblings that Mutter can stack underneath the selector");
assert.match(
    extensionSource,
    /_destroyLivePreviews\(false\)[\s\S]*?const previewRects = \[\][\s\S]*?this\._livePreviewRects = previewRects[\s\S]*?_publishPreviewPointerInside\(true\)/,
    "replacing live previews must atomically install and publish their pointer hitboxes");
assert.match(
    extensionSource,
    /const previewFadeActors = \[\][\s\S]*?previewFadeActors\.push\(preview\)[\s\S]*?for \(const preview of previewFadeActors\)[\s\S]*?preview\.ease/,
    "only thumbnail-sized actors may be animated when live previews appear");
assert.doesNotMatch(
    extensionSource,
    /Main\.layoutManager\.trackChrome\(preview, \{/,
    "live thumbnails must never enter Shell's stage input region");
assert.match(
    extensionSource,
    /const preview = new Clutter\.Actor\(\{[\s\S]*?reactive: false,[\s\S]*?opacity: 0,[\s\S]*?const selector = new St\.Widget\(\{[\s\S]*?reactive: false,[\s\S]*?previewFadeActors\.push\(preview\)[\s\S]*?for \(const preview of previewFadeActors\)[\s\S]*?preview\.ease\(\{[\s\S]*?opacity: 255/,
    "live clone actors and selectors must remain paint-only above GTK input");
assert.doesNotMatch(
    extensionSource,
    /overlay\.ease\(/,
    "the full-stage live-preview container must not be opacity animated");
assert.doesNotMatch(
    extensionSource,
    /selector\.connect\('button-(?:press|release)-event'/,
    "paint-only compositor previews must leave all mouse events to GTK");
assert.match(
    extensionSource,
    /PREVIEW_DOUBLE_CLICK_GUARD_US[\s\S]*?PREVIEW_DOUBLE_CLICK_DISTANCE_PX[\s\S]*?_previewPointerLastClick[\s\S]*?previousClick\.window === window[\s\S]*?clickTime - previousClick\.time < PREVIEW_DOUBLE_CLICK_GUARD_US[\s\S]*?Math\.abs\(sourceX - previousClick\.x\)[\s\S]*?Math\.abs\(sourceY - previousClick\.y\)[\s\S]*?return;/,
    "rapid clicks at one PiP location must not become a browser maximize double-click");
assert.match(
    extensionSource,
    /Clutter\.ButtonState\.RELEASED[\s\S]*?_schedulePreviewPointerStep\(100,[\s\S]*?window\.get_maximized[\s\S]*?Meta\.MaximizeFlags\.NONE[\s\S]*?window\.unmaximize\(Meta\.MaximizeFlags\.BOTH\)/,
    "a delayed browser PiP maximize must be reverted before it changes preview targeting");
assert.match(
    extensionSource,
    /_suppressPreviewInput\(\)[\s\S]*?_schedulePreviewPointerStep\(16,[\s\S]*?create_virtual_device\([\s\S]*?Clutter\.InputDeviceType\.POINTER_DEVICE[\s\S]*?notify_absolute_motion\([\s\S]*?_schedulePreviewPointerStep\(16,[\s\S]*?notify_button\([\s\S]*?Clutter\.ButtonState\.PRESSED[\s\S]*?_schedulePreviewPointerStep\(24,[\s\S]*?notify_button\([\s\S]*?Clutter\.ButtonState\.RELEASED[\s\S]*?_schedulePreviewPointerStep\(16,[\s\S]*?notify_absolute_motion\([\s\S]*?_restorePreviewInput\(\)/,
    "PiP mouse motion, press, release, and restoration must run after the physical grab in separate main-loop turns");
assert.match(
    extensionSource,
    /_suppressPreviewInput\(\) \{[\s\S]*?_livePreviewOverlay[\s\S]*?_dockWindow\?\.get_compositor_private[\s\S]*?_auxiliaryPosition\(window\)\?\.type !== 'preview'[\s\S]*?actor\.set_reactive\(false\)[\s\S]*?_restorePreviewInput\(\) \{[\s\S]*?actor\.set_reactive\(reactive\)/,
    "PiP forwarding must pass through overlapping GTK surfaces without hiding their painted actors");
assert.doesNotMatch(
    extensionSource,
    /_suppressPreviewInput\(\) \{[\s\S]*?actor\.hide\(\)/,
    "PiP input forwarding must not flicker Docklight surfaces");
assert.match(
    extensionSource,
    /_setPreviewInputForwarding\(true,[\s\S]*?_suppressPreviewInput\(\)[\s\S]*?notify_absolute_motion[\s\S]*?notify_absolute_motion[\s\S]*?_setPreviewInputForwarding\(false\)/,
    "Docklight must freeze GTK hover state around the synthetic PiP pointer movement");
assert.match(
    previewWindowSource,
    /set_input_forwarding[\s\S]*?m_input_forwarding[\s\S]*?signal_enter_notify_event[\s\S]*?if \(m_input_forwarding\)[\s\S]*?signal_leave_notify_event[\s\S]*?if \(m_input_forwarding\)/,
    "the GTK preview must ignore synthetic pointer crossings during PiP forwarding");
assert.match(
    extensionSource,
    /_previewPointerRestorePosition[\s\S]*?get_pointer\(\)[\s\S]*?const restore = this\._previewPointerRestorePosition[\s\S]*?restored \|\| expired[\s\S]*?_forwardPreviewPrimaryClickAt[\s\S]*?_cancelPreviewPointerInput\([^)]*\)[\s\S]*?this\._previewPointerRestorePosition = \{[\s\S]*?_cancelPreviewPointerInput\([\s\S]*?notify_absolute_motion/,
    "temporary PiP pointer injection must not leak into hover state and cancellation must restore the pointer");
assert.match(
    extensionSource,
    /_cancelPreviewPointerInput\(true\)[\s\S]*?_schedulePreviewPointerStep\(delay, callback\)[\s\S]*?catch \(error\)[\s\S]*?_cancelPreviewPointerInput\(true\)[\s\S]*?_cancelPreviewPointerInput\([\s\S]*?if \(disposeDevice\)[\s\S]*?run_dispose/,
    "the virtual PiP pointer must remain alive for queued delivery and be disposed on teardown or failure");
assert.doesNotMatch(
    extensionSource,
    /notify_touch_(?:down|up)\(/,
    "PiP input forwarding must not display Shell's synthetic-touch circle");
assert.match(
    extensionSource,
    /<method name="ForwardPreviewPrimaryClick">[\s\S]*?ForwardPreviewPrimaryClickAsync[\s\S]*?_isThumbnailCallerAuthorized\(invocation\)[\s\S]*?_forwardPreviewPrimaryClickAt/,
    "an early GTK PiP click must use the same authorized compositor input path");
assert.match(
    previewWindowSource,
    /BUTTON_PRESS_MASK[\s\S]*?forwards_live_preview_click[\s\S]*?application_auxiliary[\s\S]*?supports_gnome_live_previews\(\)[\s\S]*?signal_button_press_event[\s\S]*?return true;[\s\S]*?signal_button_release_event[\s\S]*?forwards_live_preview_click[\s\S]*?forward_gnome_preview_primary_click[\s\S]*?return true;[\s\S]*?m_activate_window\.emit/,
    "the GTK fallback must consume the PiP press and forward only after physical release");
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
    thumbnailProviderSource,
    /set_gnome_preview_color[\s\S]*?SetPreviewColor[\s\S]*?\(dddd\)/,
    "Docklight must publish preview color independently of live preview rectangles");
assert.match(
    previewWindowSource,
    /supports_gnome_live_previews\(\)[\s\S]*?set_gnome_preview_color[\s\S]*?global_x \+ m_size\.padding[\s\S]*?global_y \+ WINDOW_PADDING[\s\S]*?show_gnome_live_previews/,
    "GNOME compositor actors must align with the existing GTK card bodies");
assert.match(
    previewWindowSource,
    /const bool uses_gnome_live_previews\s*=\s*m_thumbnail_provider\s*\.supports_gnome_live_previews\(\);[\s\S]*?if \(uses_gnome_live_previews\)[\s\S]*?show_thumbnail_fallback\(entry\.id\);[\s\S]*?else[\s\S]*?request_thumbnail\(/,
    "GNOME live previews must not queue immediate parallel screenshot captures");
assert.match(
    previewWindowSource,
    /GNOME_FALLBACK_CAPTURE_DELAY_MS[\s\S]*?show_gnome_live_previews[\s\S]*?m_gnome_thumbnail_fallback[\s\S]*?generation != m_generation[\s\S]*?!get_visible\(\)[\s\S]*?!entry\.second\.has_thumbnail[\s\S]*?request_thumbnail\([\s\S]*?GNOME_FALLBACK_CAPTURE_DELAY_MS/,
    "a stable GNOME preview must cache a delayed fallback without capturing during transient hover");
assert.match(
    previewWindowSource,
    /stop_live_streams\(\)[\s\S]*?hide_gnome_live_previews\(\);[\s\S]*?m_gnome_thumbnail_fallback\.disconnect\(\)/,
    "hiding a GNOME preview must cancel its delayed fallback capture");
assert.match(
    previewWindowSource,
    /DockPreviewCardCanvas\([\s\S]*?preview_color[\s\S]*?m_preview_color\.get_red\(\)[\s\S]*?set_preview_color[\s\S]*?new DockPreviewCardCanvas\([\s\S]*?m_preview_color/,
    "GTK preview cards must render their selector with the configured preview color");
assert.match(
    previewWindowSource,
    /set_preview_color\([\s\S]*?m_thumbnail_targets[\s\S]*?target\.image->set_preview_color\([\s\S]*?set_gnome_preview_color/,
    "changing preview color must repaint existing GTK and GNOME previews");
assert.match(
    previewWindowSource,
    /start_live_streams\(\)[\s\S]*?set_gnome_preview_color[\s\S]*?desired_windows == m_live_window_ids/,
    "GNOME preview color updates must not be skipped when the window set is unchanged");
assert.match(
    previewWindowSource,
    /if \(uses_gnome_live_previews\)[\s\S]*?desired_windows\.insert\(entry\.first\);[\s\S]*?if \(!entry\.second\.minimized/,
    "GNOME live previews must include fully minimized window groups");
assert.match(
    dockWindowControllerSource,
    /set_preview_color\([\s\S]*?m_settings\.preview_color\(\)/,
    "preview rendering must receive the configured preview color");
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
    extensionSource,
    /_considerDockWindow\(window[\s\S]*?!Meta\.is_wayland_compositor\(\)[\s\S]*?return;[\s\S]*?_beginDockTransition\(\)/,
    "the GNOME Wayland extension must not hide native X11 dock actors");
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
    /_startDockVisibilityTransition\(hidden[\s\S]*?calculateDockHideOffset\(positioned\)[\s\S]*?actor\.ease\([\s\S]*?EASE_IN_QUAD[\s\S]*?EASE_OUT_QUAD/,
    "GNOME must use a native compositor transition at the screen edge");
assert.match(
    extensionSource,
    /get_string\('dock', 'location'\)[\s\S]*?this\._dockLocation = location[\s\S]*?placeDockInWorkArea\([\s\S]*?this\._dockLocation/,
    "GNOME autohide must preserve the configured dock edge");
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
    "\nthis.testApi = {calculateDockHideOffset, calculateDockRevealRect, calculateDockStrut, " +
    "clampAuxiliaryToWorkArea, inferDockEdge, " +
    "isDockPlacementCommitted, isPointerInsideDockInterior, " +
    "isSyntheticApplicationId, " +
    "parseAuxiliaryPosition, placeDockInWorkArea};";
const context = {};
vm.createContext(context);
vm.runInContext(source, context, {filename: helperPath});

const {
    calculateDockHideOffset,
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
assert.strictEqual(
    inferDockEdge(primary, {x: 0, y: 0, width: 64, height: 1080}),
    "left");
assert.strictEqual(
    inferDockEdge(primary, {x: 1106, y: 0, width: 64, height: 1080}),
    "right");
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
        {x: 0, y: 0, width: 64, height: 1080},
        "fill",
        "left")},
    {x: 0, y: 32, width: 64, height: 1080, edge: "left"});
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
assert.deepStrictEqual(
    {...calculateDockHideOffset(pointerFixtures[0].placement)},
    {x: 0, y: -64});
assert.deepStrictEqual(
    {...calculateDockHideOffset(pointerFixtures[1].placement)},
    {x: 0, y: 64});
assert.deepStrictEqual(
    {...calculateDockHideOffset(pointerFixtures[2].placement)},
    {x: -64, y: 0});
assert.deepStrictEqual(
    {...calculateDockHideOffset(pointerFixtures[3].placement)},
    {x: 64, y: 0});
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
