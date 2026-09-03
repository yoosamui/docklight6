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
const dockConstantsPath = path.resolve(
    __dirname,
    "../dock/dock_constants.h");
const tooltipManagerPath = path.resolve(
    __dirname,
    "../dock/tooltip_manager.cpp");
const previewManagerPath = path.resolve(
    __dirname,
    "../dock/preview_manager.cpp");
const layoutCoordinatorPath = path.resolve(
    __dirname,
    "../dock/layout_coordinator.cpp");
const dockTooltipWindowPath = path.resolve(
    __dirname,
    "../dock/dock_tooltip_window.cpp");
const dockWindowPath = path.resolve(
    __dirname,
    "../dock/dock_window.cpp");
const dockWindowDndPath = path.resolve(
    __dirname,
    "../dock/dock_window_dnd.cpp");
const dockWindowSurfacePath = path.resolve(
    __dirname,
    "../dock/dock_window_surface.cpp");
const dockWindowItemsPath = path.resolve(
    __dirname,
    "../dock/dock_window_items.cpp");
const legacySurfaceBackendPath = path.resolve(
    __dirname,
    "../dock/backends/legacy_dock_surface_backend.cpp");
const plasmaSurfaceBackendPath = path.resolve(
    __dirname,
    "../dock/backends/layer_shell_dock_surface_backend.cpp");
const dockLayoutTypesPath = path.resolve(
    __dirname,
    "../layout/dock_layout_types.h");
const dockItemPath = path.resolve(
    __dirname,
    "../dock/dock_item.cpp");
const dockItemContextMenuPath = path.resolve(
    __dirname,
    "../dock/dock_item_context_menu.cpp");
const dockItemEffectsPath = path.resolve(
    __dirname,
    "../dock/dock_item_effects.cpp");
const dockItemDndPath = path.resolve(
    __dirname,
    "../dock/dock_item_dnd.cpp");
const revealWindowPath = path.resolve(
    __dirname,
    "../autohide/dock_reveal_window.cpp");
const previewWindowPath = path.resolve(
    __dirname,
    "../preview/dock_preview_window.cpp");
const previewWindowInternalPath = path.resolve(
    __dirname,
    "../preview/dock_preview_window_internal.h");
const previewLayoutPath = path.resolve(
    __dirname,
    "../preview/dock_preview_layout.cpp");
const previewThumbnailCachePath = path.resolve(
    __dirname,
    "../preview/dock_preview_thumbnail_cache.cpp");
const previewAnimationPath = path.resolve(
    __dirname,
    "../preview/dock_preview_animation.cpp");
const thumbnailProviderPath = path.resolve(
    __dirname,
    "../preview/dock_window_thumbnail_provider.cpp");
const windowSystemControllerPath = path.resolve(
    __dirname,
    "../integrations/window_system_controller.cpp");
const gnomeX11WindowBackendPath = path.resolve(
    __dirname,
    "../integrations/gnome/gnome_x11_window_backend.cpp");
const desktopSessionIdentityPath = path.resolve(
    __dirname,
    "../integrations/desktop_session_identity.h");
const dockSettingsDialogPath = path.resolve(
    __dirname,
    "../dialogs/dock_settings_dialog.cpp");
const dockAboutDialogPath = path.resolve(
    __dirname,
    "../dialogs/dock_about_dialog.cpp");
const dockSessionDialogPath = path.resolve(
    __dirname,
    "../dialogs/dock_session_dialog.cpp");

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
assert.match(
    extensionSource,
    /PROTOCOL_VERSION = '10'[\s\S]*?command === 'place'[\s\S]*?_restoreApplicationPlacement\(window, workspace, geometry\)[\s\S]*?_restoreApplicationPlacement\(window, workspace, geometry\)[\s\S]*?change_workspace_by_index\(workspace - 1, false\)[\s\S]*?unmaximize\(Meta\.MaximizeFlags\.BOTH\)[\s\S]*?move_resize_frame[\s\S]*?move_frame[\s\S]*?APPLICATION_PLACEMENT_MAX_ATTEMPTS/,
    "GNOME protocol 10 must place launched Session windows through Mutter");
assert.match(
    extensionSource,
    /_untrackWindow\(window\) \{[\s\S]*?_cancelApplicationPlacement\(window\)/,
    "GNOME Session placement retries must stop with their window");
assert.match(
    extensionSource,
    /disable\(\) \{[\s\S]*?_cancelApplicationPlacements\(\)/,
    "GNOME Session placement retries must stop with the extension");
assert.match(
    extensionSource,
    /_disconnectBackend\(\) \{[\s\S]*?_cancelApplicationPlacements\(\)/,
    "GNOME Session placement retries must stop when Docklight disconnects");
const autohideControllerSource = fs.readFileSync(
    autohideControllerPath, "utf8");
const dockWindowControllerSource = fs.readFileSync(
    dockWindowControllerPath, "utf8");
const dockConstantsSource = fs.readFileSync(
    dockConstantsPath, "utf8");
const tooltipManagerSource = fs.readFileSync(
    tooltipManagerPath, "utf8");
const previewManagerSource = fs.readFileSync(
    previewManagerPath, "utf8");
const layoutCoordinatorSource = fs.readFileSync(
    layoutCoordinatorPath, "utf8");
const dockTooltipWindowSource = fs.readFileSync(
    dockTooltipWindowPath, "utf8");
const dockWindowSource = [
    dockWindowPath,
    dockWindowDndPath,
    dockWindowSurfacePath,
    dockWindowItemsPath,
].map(sourcePath => fs.readFileSync(sourcePath, "utf8")).join("\n");
const legacySurfaceBackendSource = fs.readFileSync(
    legacySurfaceBackendPath, "utf8");
const layerShellSurfaceBackendSource = fs.readFileSync(
    plasmaSurfaceBackendPath, "utf8");
const dockLayoutTypesSource = fs.readFileSync(
    dockLayoutTypesPath, "utf8");
const dockItemSource = [
    dockItemPath,
    dockItemContextMenuPath,
    dockItemEffectsPath,
    dockItemDndPath,
].map(sourcePath => fs.readFileSync(sourcePath, "utf8")).join("\n");
const registryChangedHandler = dockWindowControllerSource.match(
    /m_window_registry_changed\s*=[\s\S]*?m_window_geometry_changed\s*=/)?.[0];
const revealWindowSource = fs.readFileSync(
    revealWindowPath, "utf8");
const previewWindowSource = [
    previewWindowInternalPath,
    previewWindowPath,
    previewLayoutPath,
    previewThumbnailCachePath,
    previewAnimationPath,
].map(sourcePath => fs.readFileSync(sourcePath, "utf8")).join("\n");
const thumbnailProviderSource = fs.readFileSync(
    thumbnailProviderPath, "utf8");
const windowSystemControllerSource = fs.readFileSync(
    windowSystemControllerPath, "utf8");
const gnomeX11WindowBackendSource = fs.readFileSync(
    gnomeX11WindowBackendPath, "utf8");
const desktopSessionIdentitySource = fs.readFileSync(
    desktopSessionIdentityPath, "utf8");
const dockSettingsDialogSource = fs.readFileSync(
    dockSettingsDialogPath, "utf8");
const dockAboutDialogSource = fs.readFileSync(
    dockAboutDialogPath, "utf8");
const dockSessionDialogSource = fs.readFileSync(
    dockSessionDialogPath, "utf8");

assert.match(
    extensionSource,
    /this\._waylandIntegration\s*=\s*Meta\.is_wayland_compositor\(\);[\s\S]*?if \(this\._waylandIntegration\) \{[\s\S]*?Gio\.DBusExportedObject\.wrapJSObject\([\s\S]*?THUMBNAIL_IFACE/,
    "native GNOME X11 must not export or use the Shell thumbnail service");
assert.match(
    extensionSource,
    /if \(!this\._waylandIntegration\)[\s\S]*?return false;[\s\S]*?_considerDockWindow\(window,[\s\S]*?if \(this\._waylandIntegration\) \{[\s\S]*?_considerAuxiliaryWindow/,
    "the X11 extension mode must recognize only the explicit main-dock identity and leave every auxiliary surface to EWMH");
assert.match(
    extensionSource,
    /else \{[\s\S]*?this\._publishAnimationOnlySnapshot\(\);[\s\S]*?_publishAnimationOnlySnapshot\(\) \{[\s\S]*?BeginSnapshot[\s\S]*?CommitSnapshot[\s\S]*?\[revision, '', ''\]/,
    "the X11 animation bridge must connect with an empty snapshot and never publish application-window state");
assert.match(
    previewWindowSource,
    /is_gnome_wayland_session\(\)[\s\S]*?"Docklight 6 Preview@"[\s\S]*?else[\s\S]*?set_title\("Docklight 6 Preview"\)/,
    "native X11 previews must not expose a Shell-owned coordinate title");
assert.match(
    dockTooltipWindowSource,
    /is_gnome_wayland_session\(\)[\s\S]*?"Docklight 6 Tooltip@"[\s\S]*?else[\s\S]*?set_title\("Docklight 6 Tooltip"\)/,
    "native X11 tooltips must not expose a Shell-owned coordinate title");
assert.match(
    revealWindowSource,
    /is_gnome_wayland_session\(\)[\s\S]*?"Docklight 6 Reveal@0,0"[\s\S]*?: "Docklight 6 Reveal"/,
    "the native X11 reveal strip must not expose a Shell-owned coordinate title");

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
    /<method name="ShowLivePreviews">[\s\S]*?a\(siiii\)[\s\S]*?<method name="HideLivePreviews"\/>/,
    "the GNOME thumbnail service must expose the compositor overlay lifecycle");
assert.doesNotMatch(
    extensionSource,
    /<method name="HideLivePreviews">[\s\S]*?<arg/,
    "HideLivePreviews must remain argument-free for installed-version compatibility");
assert.match(
    extensionSource,
    /ShowLivePreviewsAsync[\s\S]*?windowActor\.get_last_child\?\.\(\)[\s\S]*?new Shell\.WindowPreviewLayout\(\)[\s\S]*?const clone = layout\.add_window\(window\)[\s\S]*?clone\.source = contentActor/,
    "premium GNOME previews must clone the live content actor without compositor window effects");
assert.match(
    extensionSource,
    /CaptureWindowAsync[\s\S]*?const content = actor\.paint_to_content\(null\)/,
    "GNOME snapshot fallback must use the WindowActor capture API");
assert.doesNotMatch(
    extensionSource,
    /const (?:surface|content)Actor\s*=\s*[^;]*get_texture\?\.\(\)/,
    "GNOME previews must not confuse MetaShapedTexture content with a ClutterActor");
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
    /_previewPointerIsInside\(\)[\s\S]*?_auxiliaryWindowSignals\.keys\(\)[\s\S]*?_auxiliaryPosition\(window\)[\s\S]*?position\?\.type !== 'preview'[\s\S]*?window\.get_frame_rect\(\)[\s\S]*?clampAuxiliaryToWorkArea\([\s\S]*?width: frame\.width[\s\S]*?height: frame\.height[\s\S]*?isPointInsideRect\(previewRect,[\s\S]*?_livePreviewRects\.some\(rect =>[\s\S]*?isPointInsideRect\(rect/,
    "GNOME preview hover must include the complete placed GTK surface before falling back to thumbnail bodies");
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
    /_destroyLivePreviews\(\{[\s\S]*?publishPointerOutside: false,[\s\S]*?preserveSession: true,[\s\S]*?\}\)[\s\S]*?const previewRects = \[\][\s\S]*?this\._livePreviewRects = previewRects[\s\S]*?_publishPreviewPointerInside\(true\)/,
    "replacing live previews must atomically install and publish their pointer hitboxes");
assert.match(
    extensionSource,
    /PREVIEW_VISIBILITY_ANIMATION_MS = 180[\s\S]*?PREVIEW_VISIBILITY_MIN_SCALE = 0\.96[\s\S]*?_finishAuxiliaryTransition\(window\)[\s\S]*?preview && Meta\.is_wayland_compositor\(\)[\s\S]*?set_pivot_point\(0\.5, 0\.5\)[\s\S]*?PREVIEW_VISIBILITY_MIN_SCALE[\s\S]*?EASE_IN_OUT_QUINT/,
    "the GNOME Wayland preview surface must use the tooltip-style centred reveal transition");
assert.match(
    extensionSource,
    /this\._previewSessionOpen = false[\s\S]*?const openingSession = !this\._previewSessionOpen[\s\S]*?this\._previewSessionOpen = true[\s\S]*?_destroyLivePreviews\(\{[\s\S]*?preserveSession: true,[\s\S]*?\}\)[\s\S]*?const waylandPreviewOpening =[\s\S]*?Meta\.is_wayland_compositor\(\) && openingSession[\s\S]*?waylandPreviewOpening \? selector : preview/,
    "adjacent GNOME Wayland preview replacements must not replay the entrance animation");
assert.match(
    extensionSource,
    /<method name="HoldLivePreviewSurface"\/>[\s\S]*?HoldLivePreviewSurfaceAsync\(_params, invocation\)[\s\S]*?_isThumbnailCallerAuthorized\(invocation\)[\s\S]*?actor\.paint_to_content\(null\)[\s\S]*?new Clutter\.Actor\(\{[\s\S]*?content_gravity: Clutter\.ContentGravity\.RESIZE_FILL[\s\S]*?Main\.uiGroup\.add_child\(shield\)[\s\S]*?set_child_above_sibling\([\s\S]*?this\._livePreviewOverlay, shield/,
    "Shell must hold the complete old preview surface beneath live clones before XWayland unmaps it");
assert.match(
    extensionSource,
    /if \(!openingSession\)[\s\S]*?_releasePreviewReplacementShield\(\)[\s\S]*?_releasePreviewReplacementShield\(\)[\s\S]*?PREVIEW_REPLACEMENT_REVEAL_DELAY_MS[\s\S]*?opacity: 0,[\s\S]*?PREVIEW_REPLACEMENT_CROSSFADE_MS[\s\S]*?_destroyPreviewReplacementShield\(\)/,
    "the held surface must crossfade only after replacement actors arrive and be destroyed on teardown");
assert.match(
    extensionSource,
    /PREVIEW_CLONE_FADE_MS = 100[\s\S]*?const animatePreviews =[\s\S]*?!Meta\.is_wayland_compositor\(\) \|\| waylandPreviewOpening[\s\S]*?opacity: animatePreviews && !waylandPreviewOpening \? 0 : 255[\s\S]*?duration: PREVIEW_CLONE_FADE_MS,[\s\S]*?EASE_OUT_QUAD/,
    "the GNOME Wayland-only reveal fix must preserve the existing GNOME X11 thumbnail fade");
assert.match(
    extensionSource,
    /_finishAuxiliaryTransition\(window\)[\s\S]*?if \(this\._previewSessionOpen\)[\s\S]*?scale_x = 1;[\s\S]*?set_opacity\(transition\.opacity\)[\s\S]*?else \{[\s\S]*?PREVIEW_VISIBILITY_INITIAL_OPACITY/,
    "a remapped GTK preview must use logical session state rather than transient overlay lifetime");
assert.match(
    extensionSource,
    /_placeAuxiliaryWindow\(window,[\s\S]*?window\.make_above\(\);[\s\S]*?window\.stick\(\);[\s\S]*?window\.raise\(\);[\s\S]*?_finishAuxiliaryTransition\(window\)/,
    "GNOME auxiliary surfaces must remain above and sticky when preview activation changes workspaces");
assert.doesNotMatch(
    extensionSource,
    /Main\.layoutManager\.trackChrome\(preview, \{/,
    "live thumbnails must never enter Shell's stage input region");
assert.match(
    extensionSource,
    /const preview = new Clutter\.Actor\(\{[\s\S]*?reactive: false,[\s\S]*?const selector = new St\.Widget\(\{[\s\S]*?reactive: false/,
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
    /m_gnome_live_previews_requested = true;[\s\S]*?hide_gnome_live_previews\([\s\S]*?if \(!m_gnome_live_previews_requested\)[\s\S]*?m_gnome_live_previews_requested = false;/,
    "GNOME live-preview teardown must be idempotent");
assert.match(
    desktopSessionIdentitySource,
    /is_wayland_session\(\)[\s\S]*?XDG_SESSION_TYPE[\s\S]*?session_type == "wayland"[\s\S]*?session_type\.empty\(\)[\s\S]*?WAYLAND_DISPLAY/,
    "the shared session identity must prefer XDG_SESSION_TYPE and only use WAYLAND_DISPLAY as a fallback");
assert.match(
    thumbnailProviderSource,
    /wayland_session\s*=\s*DesktopSessionIdentity::is_wayland_session\(\);[\s\S]*?gnome_shell_capture\s*=\s*wayland_session\s*&&[\s\S]*?DesktopSessionIdentity::[\s\S]*?identifies_gnome_shell\([\s\S]*?normalized_desktop\)/,
    "GNOME must choose the Shell thumbnail service by session type so X11 uses XComposite while XWayland presentation remains supported");
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
    /DockPreviewWindow::prime_thumbnail_cache[\s\S]*?m_thumbnail_provider\s*\.supports_gnome_live_previews\(\)[\s\S]*?return;/,
    "all GNOME live-preview sessions must skip eager thumbnail capture");
assert.match(
    previewWindowSource,
    /const auto previously_active\s*=\s*m_thumbnail_cache_active;[\s\S]*?for \(const auto &window_id\s*:\s*m_thumbnail_cache_active\)[\s\S]*?previously_active\.count\(window_id\) == 0[\s\S]*?request_active_cache_refresh\(window_id\)/,
    "caption-only updates must not recapture an unchanged active window while previews are closed");
assert.match(
    previewWindowSource,
    /GNOME_FALLBACK_CAPTURE_DELAY_MS[\s\S]*?show_gnome_live_previews[\s\S]*?m_gnome_thumbnail_fallback[\s\S]*?generation != m_generation[\s\S]*?!get_visible\(\)[\s\S]*?!entry\.second\.has_thumbnail[\s\S]*?request_thumbnail\([\s\S]*?GNOME_FALLBACK_CAPTURE_DELAY_MS/,
    "a stable GNOME preview must cache a delayed fallback without capturing during transient hover");
assert.match(
    previewWindowSource,
    /GNOME_FALLBACK_CAPTURE_DELAY_MS = 500;/,
    "GNOME fallback capture must start after the preview entrance fade settles");
assert.match(
    previewWindowSource,
    /GNOME_PREVIEW_REMAP_DELAY_MS = 34[\s\S]*?uses_wayland_session\(\)[\s\S]*?XDG_SESSION_TYPE[\s\S]*?wayland[\s\S]*?DockPreviewWindow::show_preview[\s\S]*?remap_was_pending[\s\S]*?m_replacing_gnome_wayland_preview\s*=[\s\S]*?get_mapped\(\) \|\| remap_was_pending[\s\S]*?uses_wayland_session\(\)[\s\S]*?supports_gnome_live_previews\(\)[\s\S]*?if \(m_replacing_gnome_wayland_preview\)[\s\S]*?hold_gnome_live_preview_surface\([\s\S]*?generation != m_generation[\s\S]*?set_opacity\(0\.01\);[\s\S]*?hide\(\);[\s\S]*?m_gnome_preview_remap_delay[\s\S]*?present_preview\([\s\S]*?GNOME_PREVIEW_REMAP_DELAY_MS[\s\S]*?return;[\s\S]*?set_opacity\(0\.0\);[\s\S]*?present_preview\(entries, location, position, size\)/,
    "mapped GNOME XWayland replacements must hold the old compositor surface before unmapping and resizing");
assert.match(
    previewWindowSource,
    /DockPreviewWindow::present_preview[\s\S]*?stop_live_streams\(\);[\s\S]*?rebuild\(entries, size\)[\s\S]*?resize\(size\.width, size\.height\)[\s\S]*?m_presentation_pending = true;[\s\S]*?show_all\(\);/,
    "the replacement must rebuild and remap only after its unmap settle phase");
assert.match(
    previewWindowSource,
    /signal_size_allocate[\s\S]*?complete_presentation\(\)[\s\S]*?GNOME_PREVIEW_REVEAL_DELAY_MS = 50[\s\S]*?DockPreviewWindow::complete_presentation[\s\S]*?if \(m_replacing_gnome_wayland_preview\)[\s\S]*?start_live_streams\([\s\S]*?generation != m_generation[\s\S]*?!get_visible\(\)[\s\S]*?queue_draw\(\);[\s\S]*?m_gnome_preview_reveal_delay[\s\S]*?generation == m_generation[\s\S]*?set_opacity\(1\.0\);[\s\S]*?GNOME_PREVIEW_REVEAL_DELAY_MS[\s\S]*?m_replacing_gnome_wayland_preview = false;[\s\S]*?return;[\s\S]*?start_opacity_animation\(false\);/,
    "GNOME Wayland replacements must repaint and settle after Shell acknowledgement before GTK is revealed");
assert.match(
    previewWindowSource,
    /DockPreviewWindow::start_opacity_animation[\s\S]*?uses_wayland_session\(\)[\s\S]*?supports_gnome_live_previews\(\)[\s\S]*?if \(hiding\)[\s\S]*?hide\(\);[\s\S]*?clear_cards\(\);[\s\S]*?set_opacity\(1\.0\);[\s\S]*?return;[\s\S]*?m_opacity_animation_hiding = hiding/,
    "GNOME Wayland must close GTK immediately without exposing an XWayland fade frame");
assert.match(
    thumbnailProviderSource,
    /LivePreviewsCompletion[\s\S]*?g_dbus_connection_call_finish[\s\S]*?callback\(success\)[\s\S]*?show_gnome_live_previews[\s\S]*?ready = complete_gnome_live_previews[\s\S]*?g_dbus_connection_call\([\s\S]*?ready,[\s\S]*?completion_data/,
    "ShowLivePreviews must report its asynchronous Shell acknowledgement to the preview handoff");
assert.match(
    thumbnailProviderSource,
    /hold_gnome_live_preview_surface\([\s\S]*?LivePreviewsCompletion[\s\S]*?"HoldLivePreviewSurface"[\s\S]*?complete_gnome_live_previews/,
    "the GTK remap must wait for Shell to hold the old preview surface");
assert.match(
    previewWindowSource,
    /DockPreviewWindow::apply_position[\s\S]*?if \(!m_uses_layer_shell\)[\s\S]*?get_window\(\)[\s\S]*?move_resize\([\s\S]*?global_x,[\s\S]*?global_y,[\s\S]*?width,[\s\S]*?height\)/,
    "an X11 or XWayland preview must move and resize atomically");
assert.match(
    previewWindowSource,
    /DockPreviewWindow::stop_live_streams\(\)[\s\S]*?if \(!m_replacing_gnome_wayland_preview\)[\s\S]*?hide_gnome_live_previews\(\)[\s\S]*?m_gnome_thumbnail_fallback\.disconnect\(\)/,
    "an adjacent GNOME Wayland update must preserve live actors while a real hide still tears them down");
assert.match(
    previewWindowSource,
    /DockPreviewWindow::hide_preview\(\)[\s\S]*?m_replacing_gnome_wayland_preview = false;[\s\S]*?stop_live_streams\(\)[\s\S]*?DockPreviewWindow::hide_preview_immediately\(\)[\s\S]*?m_replacing_gnome_wayland_preview = false;[\s\S]*?stop_live_streams\(\)/,
    "normal and forced GNOME closure must use the same reliable teardown contract");
assert.match(
    thumbnailProviderSource,
    /hide_gnome_live_previews\(\)[\s\S]*?"HideLivePreviews",[\s\S]*?nullptr,[\s\S]*?G_DBUS_CALL_FLAGS_NONE/,
    "the GNOME bridge must preserve the established argument-free hide call");
assert.match(
    extensionSource,
    /HideLivePreviewsAsync\(_params, invocation\)[\s\S]*?_destroyLivePreviews\(\)[\s\S]*?_destroyLivePreviews\(\{[\s\S]*?preserveSession = false[\s\S]*?if \(!preserveSession\)[\s\S]*?this\._previewSessionOpen = false[\s\S]*?this\._livePreviewOverlay\.destroy\(\)/,
    "hiding must synchronously reset session state and destroy compositor previews");
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
    /if \(!m_uses_layer_shell \|\|[\s\S]*?uses_mapped_thumbnail_cache\(\)\)[\s\S]*?m_thumbnail_cache_eligible/,
    "mapped-window thumbnail caching must remain active for native layer-shell previews");
assert.match(
    previewWindowSource,
    /!uses_mapped_thumbnail_cache\(\) &&[\s\S]*?m_uses_layer_shell[\s\S]*?m_thumbnail_cache_eligible\.count/,
    "a minimized native preview must not accept an in-flight compositor transition frame");
assert.match(
    previewWindowSource,
    /validated_capture_session[\s\S]*?uses_settled_thumbnail_capture\(\)[\s\S]*?m_thumbnail_cache_settle_epochs[\s\S]*?THUMBNAIL_RECOVERY_SETTLE_MS/,
    "a restored native preview must retain its cached frame until compositor effects settle");
assert.match(
    previewWindowSource,
    /if \(uses_gnome_live_previews\)[\s\S]*?desired_windows\.insert\(entry\.first\);[\s\S]*?if \(!entry\.second\.minimized/,
    "GNOME live previews must include fully minimized window groups");
assert.match(
    previewWindowSource,
    /has_visible_current_muffin_target[\s\S]*?uses_muffin_session\(\)[\s\S]*?!entry\.second\.minimized &&[\s\S]*?entry\.second\.on_current_desktop[\s\S]*?m_dynamic_refresh =[\s\S]*?has_visible_current_muffin_target/,
    "Muffin must keep live refresh active for visible current-workspace windows without relying on MPRIS");
assert.match(
    previewWindowSource,
    /DockPreviewWindow::request_x11_change_probe[\s\S]*?found->second\.minimized \|\|[\s\S]*?!found->second\.on_current_desktop[\s\S]*?DockPreviewWindow::request_live_x11_thumbnail[\s\S]*?found->second\.minimized \|\|[\s\S]*?!found->second\.on_current_desktop/,
    "native X11 must leave minimized and other-workspace thumbnails frozen");
assert.match(
    previewWindowSource,
    /uses_muffin_session[\s\S]*?cinnamon[\s\S]*?uses_muffin_full_live_capture[\s\S]*?if \(uses_muffin_full_live_capture \|\|[\s\S]*?request_live_x11_thumbnail[\s\S]*?if \(uses_muffin_full_live_capture\)[\s\S]*?return;[\s\S]*?request_x11_change_probe/,
    "Muffin must directly refresh every visible current-workspace card without depending on change-probe promotion");
assert.match(
    previewManagerSource,
    /set_preview_color\(settings\.preview_color\(\)\)/,
    "preview rendering must receive the configured preview color");
assert.match(
    dockWindowControllerSource,
    /set_dock_placement_geometry[\s\S]*?calculated_dock_screen_position|calculated_dock_screen_position[\s\S]*?set_dock_placement_geometry/,
    "XWayland monitor changes must publish calculated target geometry, not a stale mapped origin");
assert.match(
    legacySurfaceBackendSource,
    /GDK_IS_X11_DISPLAY[\s\S]*?capture_x11_base_workarea[\s\S]*?m_x11_base_workarea/,
    "X11 layout must not feed Docklight's own strut back into its edge margin");
assert.match(
    layoutCoordinatorSource,
    /if \(m_window\.m_window_registry && !x11_dock\)/,
    "KWin reports must not replace the authoritative XWayland work area");
assert.match(
    dockWindowControllerSource,
    /if \(!m_window\.surface_uses_native_placement\(\)\)[\s\S]*?apply_workarea_insets/,
    "layer-shell placement must not count compositor work-area insets twice");
assert.match(
    layerShellSurfaceBackendSource,
    /GDK_IS_WAYLAND_DISPLAY[\s\S]*?gtk_layer_is_supported\(\)[\s\S]*?LayerShellDockSurfaceBackend/,
    "the native layer-shell surface backend requires an actual Wayland display and layer-shell support");
assert.match(
    layerShellSurfaceBackendSource,
    /return std::make_unique<[\s\S]*?LegacyDockSurfaceBackend/,
    "X11 and Wayland compositors without layer shell must remain on the legacy surface backend");
assert.doesNotMatch(
    dockWindowSource,
    /gtk_layer_(?:init_for_window|set_monitor|set_anchor|set_margin|set_exclusive_zone|auto_exclusive_zone_enable)/,
    "DockWindow must delegate native layer-surface operations");
assert.doesNotMatch(
    dockWindowSource,
    /(?:LayerShell|Legacy)DockSurfaceBackend|GDK_IS_WAYLAND_DISPLAY|XDG_CURRENT_DESKTOP/,
    "DockWindow must not select or identify platform surface implementations");
assert.doesNotMatch(
    dockWindowControllerSource,
    /gtk_layer_set_monitor/,
    "DockWindowController must delegate main-surface monitor assignment");
assert.doesNotMatch(
    legacySurfaceBackendSource,
    /gtk_layer_/,
    "the legacy X11 and ordinary-Wayland backend must not apply layer-surface operations");
for (const operation of [
    "gtk_layer_init_for_window",
    "gtk_layer_set_monitor",
    "gtk_layer_set_anchor",
    "gtk_layer_set_margin",
    "gtk_layer_set_exclusive_zone",
    "gtk_layer_auto_exclusive_zone_enable"
]) {
    assert.ok(
        layerShellSurfaceBackendSource.includes(operation),
        `the native layer-shell surface backend must own ${operation}`);
}
assert.match(
    legacySurfaceBackendSource,
    /if \(is_gnome_wayland_session\(\) \|\|[\s\S]*?is_gnome_x11_session\(\) \|\|[\s\S]*?is_kde_wayland_session\(\) \|\|[\s\S]*?is_cinnamon_x11_session\(\)\)[\s\S]*?x11_scoped_monitor_workarea/,
    "GNOME and Cinnamon X11 must preserve their monitor-scoped GTK panel work areas");
assert.match(
    legacySurfaceBackendSource,
    /reusable_gnome_x11_workarea[\s\S]*?m_x11_base_output[\s\S]*?if \(!reusable_gnome_x11_workarea\)[\s\S]*?x11_scoped_monitor_workarea/,
    "a GNOME X11 edge change must not recapture DockLight's previous strut as native work area");
assert.match(
    dockWindowControllerSource,
    /monitor_geometry_changed[\s\S]*?output_changed[\s\S]*?prepare_surface_change\(\)[\s\S]*?m_autohide_controller->set_monitor/,
    "moving or resizing the selected output must invalidate cached X11 placement state");
assert.match(
    windowSystemControllerSource,
    /const bool gnome_shell_x11\s*=[\s\S]*?is_gnome_shell_x11_session\(\);[\s\S]*?case X11BackendKind::mutter:[\s\S]*?if \(gnome_shell_x11\)[\s\S]*?GnomeX11WindowBackend>[\s\S]*?else[\s\S]*?MutterWindowBackend>/,
    "only an explicit GNOME Shell X11 identity may select the hybrid backend; standalone Mutter must keep the original backend");
assert.match(
    gnomeX11WindowBackendSource,
    /MutterWindowBackend::start\(\)[\s\S]*?m_shell_backend\.start\(\)[\s\S]*?KWinIntegrationService[\s\S]*?set_dock_placement_geometry[\s\S]*?set_dock_hidden[\s\S]*?signal_dock_animation_completed/,
    "the GNOME X11 hybrid must preserve EWMH as authoritative and mirror only dock animation state through Shell");
assert.match(
    windowSystemControllerSource,
    /std::optional<bool> g_x11_compositor_cache[\s\S]*?x11_compositor_is_running_cached\(\)[\s\S]*?g_x11_compositor_cache\.has_value\(\)[\s\S]*?XOpenDisplay\(nullptr\)[\s\S]*?g_x11_compositor_cache = running/,
    "X11 compositor detection must open DISPLAY at most once per process");
assert.match(
    windowSystemControllerSource,
    /x11 && x11_compositor_is_running_cached\(\)/,
    "generic startup must use the cached X11 compositor result");
assert.match(
    dockConstantsSource,
    /PREVIEW_INPUT_FORWARDING_RESET_MS[\s\S]*?INITIAL_X11_WORKAREA_SAMPLE_INTERVAL_MS[\s\S]*?INITIAL_X11_PLACEMENT_POLL_INTERVAL_MS[\s\S]*?EDGE_LAYOUT_SETTLE_DELAY_MS/,
    "dock timing policy must remain centralized under semantic names");
assert.match(
    dockWindowControllerSource,
    /DockConstants::[\s\S]*?INITIAL_X11_WORKAREA_SAMPLE_INTERVAL_MS[\s\S]*?DockConstants::[\s\S]*?INITIAL_X11_PLACEMENT_POLL_INTERVAL_MS[\s\S]*?DockConstants::[\s\S]*?EDGE_LAYOUT_SETTLE_DELAY_MS/,
    "controller timers must consume centralized dock timing constants");
assert.match(
    previewManagerSource,
    /DockConstants::[\s\S]*?PREVIEW_INPUT_FORWARDING_RESET_MS/,
    "preview input forwarding must consume its centralized reset timeout");
assert.match(
    windowSystemControllerSource,
    /detected_window_manager[\s\S]*?identifies_gnome_shell\(desktop\)[\s\S]*?return "Mutter";[\s\S]*?wnck_handle_new/,
    "known GNOME sessions must not initialize libwnck only to identify Mutter");
assert.match(
    desktopSessionIdentitySource,
    /identifies_gnome_flashback[\s\S]*?gnome-flashback[\s\S]*?identifies_gnome_shell[\s\S]*?!identifies_gnome_flashback/,
    "GNOME Flashback must not be identified as GNOME Shell");
assert.match(
    windowSystemControllerSource,
    /const bool gnome_shell\s*=\s*DesktopSessionIdentity::[\s\S]*?identifies_gnome_shell\(desktop\)/,
    "backend startup must distinguish GNOME Shell from GNOME Flashback");
assert.match(
    dockWindowControllerSource,
    /m_initial_x11_workarea_pending\s*=\s*true;[\s\S]*?begin_initial_x11_startup\(\)[\s\S]*?finish_initial_x11_placement[\s\S]*?complete_initial_x11_startup\(\)/,
    "native X11 startup must remain guarded until final placement chooses its first visibility state");
assert.doesNotMatch(
    dockWindowControllerSource,
    /finish_initial_x11_placement\(\)[\s\S]*?set_opacity\(1\.0\)[\s\S]*?void DockWindowController::apply_configuration/,
    "final X11 placement must not expose the dock before autohide settles");
assert.match(
    autohideControllerSource,
    /complete_initial_x11_startup[\s\S]*?pointer_is_inside\(\)[\s\S]*?hide_immediately_for_x11_startup[\s\S]*?reveal_immediately/,
    "the first stable X11 frame must honor autohide and the live pointer without an intermediate reveal");
assert.match(
    autohideControllerSource,
    /reset_local_visual_transform[\s\S]*?m_initial_x11_startup_pending[\s\S]*?0\.0[\s\S]*?1\.0/,
    "layout resets must preserve the transparent X11 startup guard");
assert.match(
    tooltipManagerSource,
    /requested_item == m_hovered_item/,
    "a delayed tooltip must still belong to the currently hovered item");
assert.ok(
    registryChangedHandler,
    "the window-registry change handler must remain discoverable");
assert.doesNotMatch(
    registryChangedHandler,
    /cancel_show_timer\(\)/,
    "mapping an X11 tooltip must not cancel the next item's reveal timer");
assert.match(
    tooltipManagerSource,
    /m_hovered_item == &item &&[\s\S]*?m_pending_item == &item[\s\S]*?m_visible_item == &item/,
    "a stale hovered-item pointer must not suppress a new tooltip request");
assert.match(
    dockItemSource,
    /DockItem::on_enter_notify_event[\s\S]*?schedule_show_tooltip[\s\S]*?DockItem::on_leave_notify_event[\s\S]*?schedule_hide_tooltip/,
    "each item must start and cancel tooltip timing from its own crossing events");
assert.match(
    dockWindowControllerSource,
    /signal_dock_pointer_inside_changed\(\)[\s\S]*?m_backend_dock_pointer_state_known = true;[\s\S]*?m_backend_dock_pointer_inside = inside;[\s\S]*?DockWindowController::dock_pointer_inside\(\) const[\s\S]*?m_backend_dock_pointer_state_known[\s\S]*?m_backend_dock_pointer_inside[\s\S]*?m_window\.pointer_is_inside\(\)[\s\S]*?DockWindowController::start_hide_timer\(\)[\s\S]*?dock_pointer_inside\(\)/,
    "GNOME pointer tracking must override the oversized XWayland dock surface when closing overlays");
assert.match(
    dockWindowControllerSource,
    /signal_dock_pointer_inside_changed\(\)[\s\S]*?set_backend_pointer_inside\(inside\);[\s\S]*?if \(inside\)[\s\S]*?cancel_hide_timer\(\);[\s\S]*?else[\s\S]*?start_hide_timer\(\);/,
    "leaving the Shell-tracked dock must restart overlay closure even after an item-level timer expired inside dock padding");
assert.match(
    previewManagerSource + dockWindowControllerSource,
    /PreviewManager::PreviewManager[\s\S]*?std::function<bool\(\)> is_dock_pointer_inside[\s\S]*?m_is_dock_pointer_inside\(std::move\(is_dock_pointer_inside\)\)[\s\S]*?PreviewManager::hide[\s\S]*?m_autohide\.uninhibit\([\s\S]*?m_tooltips\.pointer_inside\(\) \|\| m_is_dock_pointer_inside\(\)[\s\S]*?make_unique<PreviewManager>[\s\S]*?\[this\]\(\) \{ return dock_pointer_inside\(\); \}/,
    "closing a GNOME preview must release autohide with Shell's authoritative dock-pointer state");
assert.match(
    previewManagerSource,
    /PreviewManager::set_shell_pointer_inside[\s\S]*?m_preview_desktop_id\.empty\(\)[\s\S]*?m_shell_pointer_state_known = true;[\s\S]*?PreviewManager::pointer_inside\(\) const[\s\S]*?m_shell_pointer_state_known[\s\S]*?\? m_shell_pointer_inside[\s\S]*?: m_pointer_inside/,
    "Shell preview-pointer state must override stale GTK crossings while a GNOME preview is open");
assert.match(
    tooltipManagerSource,
    /TooltipManager::schedule_show[\s\S]*?hide\(\);[\s\S]*?start_show_timer\(item, text/,
    "adjacent dock items must fade the previous tooltip before the delayed reveal");
assert.match(
    dockTooltipWindowSource,
    /update_mapped_tooltip\s*=\s*[\s\S]*?m_has_request && get_mapped\(\)[\s\S]*?if \(!update_mapped_tooltip\)[\s\S]*?hide\(\);[\s\S]*?apply_position\([\s\S]*?if \(update_mapped_tooltip\)[\s\S]*?set_opacity\(1\.0\);[\s\S]*?return;[\s\S]*?m_reveal_timer/,
    "a mapped tooltip update must not unmap or replay its reveal animation");
assert.match(
    dockTooltipWindowSource,
    /DockTooltipWindow::apply_position[\s\S]*?if \(!m_uses_layer_shell\)[\s\S]*?get_window\(\)[\s\S]*?move_resize\([\s\S]*?global_x,[\s\S]*?global_y,[\s\S]*?width,[\s\S]*?height\)/,
    "an XWayland tooltip must move and resize atomically between differently sized labels");
assert.match(
    dockTooltipWindowSource,
    /smootherstep\([\s\S]*?progress \* progress \* progress \*[\s\S]*?progress \* 6\.0 - 15\.0[\s\S]*?TOOLTIP_MIN_SCALE[\s\S]*?m_visual_scale/,
    "tooltip hide and reveal effects must use centred scale with smooth endpoint easing");
assert.match(
    previewManagerSource,
    /PreviewManager::schedule_show[\s\S]*?m_tooltips\.hide\(\);[\s\S]*?m_settings\.display_tooltips\(\) &&[\s\S]*?!m_settings\.display_preview\(\)[\s\S]*?m_tooltips\.schedule_show/,
    "populated groups must only show a tooltip when previews are disabled");
assert.match(
    previewWindowSource,
    /last_card =[\s\S]*?m_window_ids\.back\(\) == window_id;[\s\S]*?m_close_pointer_origin_valid =[\s\S]*?last_card;[\s\S]*?m_close_window\.emit\([\s\S]*?window_id,[\s\S]*?last_card\)/,
    "the Preview close action must identify the last displayed card before the group changes");
assert.match(
    previewWindowSource + previewManagerSource,
    /on_motion_notify_event[\s\S]*?m_close_pointer_origin_valid[\s\S]*?event->x_root - m_close_pointer_root_x[\s\S]*?event->y_root - m_close_pointer_root_y[\s\S]*?m_pointer_moved\.emit\(\)[\s\S]*?signal_pointer_moved\(\)[\s\S]*?m_last_card_close_pending = false;[\s\S]*?signal_pointer_left\(\)[\s\S]*?if \(m_last_card_close_pending\)[\s\S]*?return;[\s\S]*?m_signal_pointer_left\.emit\(\)/,
    "only a stationary-pointer leave caused by the last-card close may bypass normal Preview leave handling");
assert.match(
    previewManagerSource,
    /if \(entries\.empty\(\)\)[\s\S]*?hide\(\);[\s\S]*?if \(excluded_window_id\.empty\(\)\)/,
    "closing the final card must close the empty Preview layer intentionally");
assert.match(
    previewManagerSource,
    /PreviewManager::hide\([\s\S]*?m_last_card_close_pending = false;[\s\S]*?hide_preview\(\)/,
    "hiding the Preview must reset the final-card interaction state");
assert.match(
    dockItemSource,
    /signal_button_press_event[\s\S]*?GDK_BUTTON_SECONDARY[\s\S]*?outside_menu[\s\S]*?m_context_menu\.popdown\(\)/,
    "the context menu must catch an outside secondary press consumed by its pointer grab");
assert.match(
    dockItemSource,
    /signal_unmap[\s\S]*?m_context_menu_secondary_dismissed[\s\S]*?schedule_show_tooltip[\s\S]*?uninhibit_autohide\(true\)/,
    "a secondary-button menu dismissal must restore preview and pointer-inside autohide state");
assert.match(
    dockWindowSource,
    /DockWindow::schedule_show_tooltip[\s\S]*?window_entries\(\)\.empty\(\)[\s\S]*?schedule_show_tooltip/,
    "empty dock-item groups must keep their tooltip path");
assert.match(
    dockWindowSource,
    /DockWindow::dock_items\(\) const[\s\S]*?return m_dock_items_cache;/,
    "DockWindow must return its typed DockItem cache without traversing GTK children");
assert.doesNotMatch(
    dockWindowSource,
    /dynamic_cast<DockItem/,
    "DockWindow must not rediscover typed items with repeated dynamic casts");
assert.match(
    dockWindowSource,
    /DockWindow::register_dock_item[\s\S]*?m_dock_items_cache\.push_back\(item\)[\s\S]*?m_dock_box\.pack_start[\s\S]*?DockWindow::unregister_dock_item[\s\S]*?m_dock_items_cache\.erase\(item_position\)[\s\S]*?m_dock_box\.remove/,
    "DockItem container mutations must update the typed cache before GTK signals fire");
assert.match(
    dockWindowSource,
    /DockWindow::apply_dragged_item_order[\s\S]*?m_dock_items_cache = items;[\s\S]*?DockWindow::synchronize_dock_items[\s\S]*?register_dock_item\(item\)[\s\S]*?unregister_dock_item\(item\)[\s\S]*?m_dock_items_cache = ordered_items;/,
    "drag and synchronization reorder operations must preserve visual cache order");
assert.match(
    dockWindowSource,
    /normalized_attached_ids ==[\s\S]*?m_synchronized_attached_ids[\s\S]*?normalized_running_ids ==[\s\S]*?m_synchronized_running_ids[\s\S]*?dock_structure_changed[\s\S]*?current_items\.size\(\) != desired_items\.size\(\)[\s\S]*?current_id != desired_id[\s\S]*?refresh_indicator\(\);[\s\S]*?return;/,
    "dock synchronization must avoid GTK mutations for both title-only and structurally identical updates");
assert.match(
    dockWindowSource,
    /gtk_widget_freeze_child_notify[\s\S]*?m_dock_box\.reorder_child[\s\S]*?gtk_widget_thaw_child_notify/,
    "bulk dock reordering must coalesce GTK child-property notifications");
assert.match(
    dockWindowControllerSource + previewManagerSource,
    /schedule_show\([\s\S]*?m_settings\.preview_show_delay\(\)[\s\S]*?m_show_timer[\s\S]*?show_delay_ms\);/,
    "populated dock items must use only the configured preview delay");
assert.match(
    dockWindowControllerSource + previewManagerSource,
    /DockWindowController::hide_tooltip_immediately[\s\S]*?m_preview_manager->hide_immediately[\s\S]*?PreviewManager::hide_immediately[\s\S]*?hide\(\)[\s\S]*?hide_preview_immediately\(\)/,
    "immediate preview closure must clear controller state before hiding the surface");
assert.match(
    revealWindowSource,
    /DockRevealWindow::set_monitor[\s\S]*?m_monitor_geometry[\s\S]*?m_has_placement[\s\S]*?apply_x11_placement\(\)/,
    "an X11 reveal strip must reapply its placement after its monitor geometry changes");
assert.match(
    revealWindowSource,
    /reveal_window_type\(\)[\s\S]*?GDK_IS_X11_DISPLAY\(display\)[\s\S]*?Gtk::WINDOW_POPUP[\s\S]*?Gtk::WINDOW_TOPLEVEL[\s\S]*?DockRevealWindow::DockRevealWindow\(\)[\s\S]*?Gtk::Window\(reveal_window_type\(\)\)/,
    "the native X11 reveal strip must remain outside asynchronous window-manager placement");
assert.match(
    revealWindowSource,
    /DockRevealWindow::start_x11_edge_poll[\s\S]*?uses_xwayland_presentation\(\) &&[\s\S]*?!x11_reveal_surface_is_inset\(\)[\s\S]*?return;[\s\S]*?signal_timeout/,
    "native X11 must poll the physical edge while XWayland retains its inset-only fallback");
assert.match(
    revealWindowSource,
    /uses_xwayland_presentation[\s\S]*?DOCKLIGHT_XWAYLAND_PRESENTATION/,
    "physical-edge polling must distinguish native X11 from an XWayland presentation");
assert.match(
    extensionSource,
    /_isX11DockWindow\(window\)[\s\S]*?_removeDockStrut\(\)[\s\S]*?_publishDockSurfaceGeometry\(rect\)/,
    "GNOME must leave XWayland dock placement and reservation to EWMH");
assert.match(
    extensionSource,
    /_beginDockTransition\(\)[\s\S]*?if \(!Meta\.is_wayland_compositor\(\)\) \{[\s\S]*?this\._dockActor = null;[\s\S]*?return;[\s\S]*?actor\.set_opacity\(0\)/,
    "GNOME must keep native X11 docks out of the Wayland actor transition lifecycle without excluding XWayland docks");
assert.match(
    extensionSource,
    /\['unmanaged', \(\) => this\._clearDockWindow\(true\)\][\s\S]*?_clearDockWindow\(unmanaged = false\)[\s\S]*?if \(unmanaged\) \{[\s\S]*?this\._dockActor = null;/,
    "GNOME must not dereference a disposed dock actor from its unmanaged callback");
assert.match(
    autohideControllerSource,
    /hide_now\([\s\S]*?\)[\s\S]*?if \(uses_shell_reveal_trigger\(\)\)[\s\S]*?request_shell_visibility\(true\);\s*return;/,
    "GNOME autohide must keep the placed dock mapped instead of remapping at the centre");
assert.match(
    dockLayoutTypesSource,
    /enum class DockAutohideEffect[\s\S]*?plasma[\s\S]*?gnome[\s\S]*?slide[\s\S]*?fade[\s\S]*?scale[\s\S]*?slide_fade/,
    "autohide effect selection must use one desktop-neutral common type");
assert.match(
    layerShellSurfaceBackendSource,
    /default_autohide_effect\(\) const[\s\S]*?DockAutohideEffect::plasma/,
    "Plasma Wayland must retain its current surface effect");
assert.match(
    layerShellSurfaceBackendSource,
    /configurable_autohide_effects\(\) const[\s\S]*?DockAutohideEffect::plasma[\s\S]*?DockAutohideEffect::slide/,
    "Plasma Wayland settings must retain Plasma and add movement-only Slide as a separate choice");
assert.match(
    layerShellSurfaceBackendSource,
    /supports_autohide_slide\(\) const[\s\S]*?return true;[\s\S]*?set_autohide_slide_progress\([\s\S]*?autohide_slide_content_offset\([\s\S]*?set_surface_horizontal_offset\([\s\S]*?set_surface_vertical_offset\([\s\S]*?set_opacity\(1\.0\)/,
    "Plasma Wayland must own movement-only slide drawing on both axes without fading");
assert.match(
    layerShellSurfaceBackendSource,
    /finish_autohide_slide\(\s*bool\)\s*\{[\s\S]*?fully clipped layer surface mapped[\s\S]*?\}/,
    "Plasma Wayland Slide must remain mapped while hidden so KWin cannot add a conflicting reveal transform");
assert.match(
    legacySurfaceBackendSource,
    /default_autohide_effect\(\) const[\s\S]*?uses_gnome_wayland_autohide_effect\(\)[\s\S]*?DockAutohideEffect::gnome[\s\S]*?DesktopSessionIdentity::[\s\S]*?is_gnome_shell_x11_session\(\)[\s\S]*?DockAutohideEffect::gnome[\s\S]*?m_native_x11[\s\S]*?DockAutohideEffect::plasma[\s\S]*?DockAutohideEffect::slide/,
    "GNOME X11 must default to its Shell effect while every other native X11 backend retains Plasma-style behavior");
assert.match(
    legacySurfaceBackendSource,
    /set_type_hint\([\s\S]*?WINDOW_TYPE_HINT_DOCK[\s\S]*?set_keep_above\(true\)/,
    "the dock must retain its established EWMH type and keep-above policy");
assert.match(
    legacySurfaceBackendSource,
    /configurable_autohide_effects\(\) const[\s\S]*?if \(!m_native_x11\)[\s\S]*?return \{[\s\S]*?DockAutohideEffect::plasma,[\s\S]*?DockAutohideEffect::slide\};/,
    "native X11 settings must offer Plasma and Slide without standalone Fade");
assert.match(
    legacySurfaceBackendSource,
    /configurable_autohide_effects\(\) const[\s\S]*?uses_gnome_wayland_autohide_effect\(\)[\s\S]*?DockAutohideEffect::gnome[\s\S]*?DockAutohideEffect::slide_fade/,
    "GNOME Wayland settings must retain the GNOME effect and add slide/fade");
assert.match(
    dockSettingsDialogSource,
    /case DockAutohideEffect::fade:\s*break;/,
    "the settings dialog must not expose standalone Fade even if a backend reports it");
assert.match(
    dockSettingsDialogSource,
    /gnome_wayland_effects[\s\S]*?DockAutohideEffect::gnome[\s\S]*?case DockAutohideEffect::slide_fade:[\s\S]*?gnome_wayland_effects[\s\S]*?"Slide"[\s\S]*?:[\s\S]*?"Slide and Fade"/,
    "GNOME Wayland must name its slide/fade effect Slide without renaming it on other backends");
assert.match(
    dockWindowControllerSource,
    /set_effect\([\s\S]*?m_window\.effective_autohide_effect\(\)[\s\S]*?m_autohide_controller->initialize\(\)/,
    "the shared autohide controller must own the backend-normalized effect");
assert.match(
    autohideControllerSource,
    /can_animate_x11\(\) const[\s\S]*?m_effect == DockAutohideEffect::slide[\s\S]*?m_effect == DockAutohideEffect::plasma[\s\S]*?m_effect == DockAutohideEffect::gnome[\s\S]*?!uses_shell_autohide_animation\(\)[\s\S]*?surface_is_native_x11\(\)/,
    "native X11 must retain its local animation whenever the optional Shell animation bridge is unavailable");
assert.match(
    autohideControllerSource,
    /m_shell_animation_active[\s\S]*?hide_immediately_for_x11_startup\(\)[\s\S]*?m_shell_animation_active = false;[\s\S]*?hide_now\([\s\S]*?uses_shell_autohide_animation\(\)[\s\S]*?m_shell_animation_active = true;[\s\S]*?reveal\(\)[\s\S]*?m_shell_animation_active &&[\s\S]*?uses_shell_autohide_animation\(\)/,
    "a dock hidden by the native fallback must finish that cycle natively before a newly available Shell bridge takes ownership");
assert.match(
    autohideControllerSource,
    /uses_shell_autohide_animation\(\) const[\s\S]*?surface_delegates_autohide_effect\([\s\S]*?provides_dock_autohide_animation[\s\S]*?dock_surface_geometry\(\)[\s\S]*?has_value\(\)/,
    "Shell animation ownership must require both the narrow capability and a discovered dock actor");
assert.match(
    autohideControllerSource,
    /uses_shell_reveal_trigger\(\) const[\s\S]*?surface_delegates_autohide_effect\([\s\S]*?m_effect[\s\S]*?has_shell_reveal_trigger\(\)/,
    "compositor-owned effects must remain routed through the active surface backend and Shell integration");
assert.match(
    dockWindowSource,
    /effective_autohide_effect\(\) const[\s\S]*?default_autohide_effect\(\)[\s\S]*?autohide_effect\(\)[\s\S]*?configurable_autohide_effects\(\)[\s\S]*?std::find\([\s\S]*?return platform_default/,
    "unsupported desktop-specific settings must fall back to the active backend default");
assert.match(
    dockWindowSource,
    /DockAutohideEffect::slide_fade[\s\S]*?supports_autohide_slide\(\)[\s\S]*?return DockAutohideEffect::slide/,
    "Plasma Wayland must migrate its former slide/fade setting to movement-only Slide without changing GNOME");
assert.match(
    autohideControllerSource,
    /case DockAutohideEffect::fade:[\s\S]*?animate_fade\(hiding\)/,
    "fade must have an explicit visual-transition dispatch");
assert.match(
    autohideControllerSource,
    /case DockAutohideEffect::slide:[\s\S]*?surface_supports_autohide_slide\(\)[\s\S]*?animate_surface_slide\(hiding\)[\s\S]*?animate_surface_slide\([\s\S]*?surface_autohide_slide_progress\(\)[\s\S]*?progress \* progress \* \(3\.0 - 2\.0 \* progress\)[\s\S]*?finish_surface_autohide_slide\(true\)/,
    "Plasma Wayland Slide must reverse from current progress and ease smoothly at both endpoints");
assert.match(
    autohideControllerSource,
    /m_pending_surface_slide_reveal[\s\S]*?animate_surface_slide\(false\)[\s\S]*?defer_surface_slide_reveal[\s\S]*?set_surface_autohide_slide_progress\([\s\S]*?m_window\.show\(\)/,
    "Plasma Wayland Slide must start reveal only after its hidden-offset surface maps");
assert.match(
    autohideControllerSource,
    /animate_fade\([\s\S]*?cancel_animation\(\)[\s\S]*?surface_autohide_fade_opacity\(\)[\s\S]*?advance_fade_animation[\s\S]*?set_surface_input_passthrough\(true\)[\s\S]*?finish_surface_autohide_fade\(true\)/,
    "local fade must reverse from current opacity and preserve each backend's final hidden state");
assert.match(
    legacySurfaceBackendSource,
    /delegates_autohide_effect\([\s\S]*?uses_gnome_wayland_autohide_effect\(\)[\s\S]*?DockAutohideEffect::fade[\s\S]*?DockAutohideEffect::slide_fade[\s\S]*?finish_autohide_fade\([\s\S]*?!m_native_x11[\s\S]*?m_window\.hide\(\)/,
    "the legacy backend must delegate GNOME compositor effects while retaining X11's mapped hidden surface");
assert.match(
    legacySurfaceBackendSource,
    /is_gnome_shell_x11_session\(\)[\s\S]*?m_native_x11[\s\S]*?DockAutohideEffect::gnome[\s\S]*?DockAutohideEffect::plasma/,
    "GNOME Shell X11 must delegate persisted Plasma choices instead of silently bypassing its healthy extension");
assert.match(
    layerShellSurfaceBackendSource,
    /set_autohide_fade_opacity\([\s\S]*?m_window\.set_opacity\(opacity\)[\s\S]*?finish_autohide_fade\([\s\S]*?m_window\.hide\(\)/,
    "Plasma fade must use layer-surface opacity before its existing unmapped hidden state");
assert.match(
    extensionSource,
    /get_string\([\s\S]*?'dock', 'autohide_effect'[\s\S]*?\['fade', 'slide_fade'\]\.includes\(configuredEffect\)[\s\S]*?this\._dockAutohideEffect = autohideEffect/,
    "GNOME must consume its compositor-effect selections from application configuration");
assert.match(
    extensionSource,
    /_dockAutohideEffect === 'fade'[\s\S]*?_startDockFadeTransition\(hidden, actor\)[\s\S]*?_startDockFadeTransition\(hidden, actor\)[\s\S]*?actor\.remove_all_transitions\(\)[\s\S]*?const startOpacity = actor\.get_opacity\(\)[\s\S]*?actor\.ease\(\{[\s\S]*?opacity: targetOpacity/,
    "GNOME fade must use its compositor actor and reverse from the current opacity");
assert.match(
    extensionSource,
    /_dockAutohideEffect !== 'slide_fade'[\s\S]*?_startDockPlasmaStyleTransition\(hidden, actor\)[\s\S]*?calculateDockHideOffset\(positioned\)[\s\S]*?EASE_IN_CUBIC[\s\S]*?EASE_OUT_CUBIC[\s\S]*?animation\.opacity = hidden \? 0 : this\._dockActorOpacity[\s\S]*?actor\.ease\(animation\)/,
    "GNOME slide/fade must combine outward movement and opacity with Plasma timing");
assert.match(
    extensionSource,
    /_startDockPlasmaStyleTransition\(hidden, actor\)[\s\S]*?set_pivot_point\(0\.5, 0\.5\)[\s\S]*?startScaleX = actor\.scale_x[\s\S]*?startScaleY = actor\.scale_y[\s\S]*?targetScale = hidden \? 0 : 1[\s\S]*?targetOpacity = hidden \? 0 : this\._dockActorOpacity[\s\S]*?animation\.scale_x = targetScale[\s\S]*?animation\.scale_y = targetScale[\s\S]*?actor\.ease\(animation\)/,
    "the GNOME effect must match Plasma Wayland centred two-axis scale and fade");
assert.match(
    autohideControllerSource,
    /finish_shell_animation\([\s\S]*?hidden[\s\S]*?ShellDockState::hidden[\s\S]*?set_surface_input_passthrough\(true\)/,
    "input pass-through must begin only after Shell completes the hide animation");
assert.match(
    extensionSource,
    /_startDockVisibilityTransition\(hidden[\s\S]*?_dockAutohideEffect !== 'slide_fade'[\s\S]*?calculateDockHideOffset\(positioned\)[\s\S]*?_updateDockAnimationClip\(actor, positioned, base\)[\s\S]*?actor\.ease\(animation\)/,
    "GNOME slide/fade must retain compositor movement and monitor clipping");
assert.match(
    extensionSource,
    /get_string\('dock', 'location'\)[\s\S]*?this\._dockLocation = location[\s\S]*?placeDockInWorkArea\([\s\S]*?this\._dockLocation/,
    "GNOME autohide must preserve the configured dock edge");
assert.match(
    extensionSource,
    /remainingAmount < \(collapseRight \? 0\.001 : 0\.5\)[\s\S]*?completeTransition\(\)[\s\S]*?return;[\s\S]*?actor\.ease/,
    "an already-positioned actor must still complete the visibility state change");
assert.match(
    extensionSource,
    /rightHideCorridorIntersectsMonitor\([\s\S]*?collapseRight[\s\S]*?set_pivot_point\(1, 0\.5\)[\s\S]*?targetScaleX[\s\S]*?animation\.scale_x = targetScaleX/,
    "a RIGHT dock facing another monitor must collapse into its fixed outer edge");
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
    /set_backend_pointer_inside\([\s\S]*?uses_shell_reveal_trigger\(\)[\s\S]*?m_shell_pointer_inside = inside[\s\S]*?ShellDockState::hiding[\s\S]*?reveal\(\)/,
    "authoritative Shell pointer entry must reverse an in-progress hide");
assert.match(
    autohideControllerSource,
    /pointer_inside\(\) const[\s\S]*?uses_shell_reveal_trigger\(\)[\s\S]*?return m_shell_pointer_inside[\s\S]*?uses_backend_pointer_tracking\(\)[\s\S]*?return m_backend_pointer_inside[\s\S]*?return m_pointer_inside/,
    "Shell/backend pointer ownership must replace stale GTK crossing state");
assert.match(
    autohideControllerSource,
    /set_backend_pointer_inside\([\s\S]*?uses_backend_pointer_tracking\(\)[\s\S]*?m_backend_pointer_inside = inside[\s\S]*?if \(m_hidden\)[\s\S]*?cancel_hide\(\)[\s\S]*?schedule_hide\(false\)/,
    "authoritative backend pointer exit must schedule an XWayland hide");
assert.match(
    autohideControllerSource,
    /reveal\(\)[\s\S]*?if \(uses_shell_reveal_trigger\(\)\)[\s\S]*?request_shell_visibility\(false\);/,
    "a Shell reveal must restore the existing mapped dock surface");
assert.match(
    autohideControllerSource,
    /else[\s\S]*?current_x = hidden\.x;[\s\S]*?m_window\.move\(current_x, current_y\);[\s\S]*?m_window\.set_opacity\(\s*X11_REVEAL_INITIAL_OPACITY\)/,
    "an X11 reveal must move to its hidden edge before becoming visible");
assert.match(
    autohideControllerSource,
    /constexpr double X11_REVEAL_INITIAL_OPACITY = 0\.0;/,
    "a remapped X11 dock must stay transparent until its hidden-edge transform reaches the compositor");
assert.match(
    autohideControllerSource,
    /if \(m_animating_to_hidden\)[\s\S]*?m_window\.set_opacity\(0\.0\);[\s\S]*?set_surface_input_passthrough\(true\);[\s\S]*?else[\s\S]*?reset_local_visual_transform\(\);/,
    "a hidden native X11 dock must remain mapped and input-pass-through so reveal does not trigger a compositor map effect");
assert.match(
    autohideControllerSource,
    /hide_now\([\s\S]*?m_window\.hide_tooltip_immediately\(\);[\s\S]*?show_reveal_trigger\(\);[\s\S]*?surface_is_native_x11\(\)[\s\S]*?set_surface_input_passthrough\(true\);[\s\S]*?animate_effect\(true\);/,
    "native X11 input must be disabled before hiding so XFWM crossing events cannot reopen overlays during the transition");
assert.match(
    autohideControllerSource,
    /show_reveal_trigger\(\)[\s\S]*?uses_backend_screen_edge_reveal\(\)[\s\S]*?m_reveal_window\.hide\(\);[\s\S]*?return;[\s\S]*?m_reveal_window\.show\(\);/,
    "Plasma Wayland must not map its GTK reveal strip underneath a stationary edge pointer");
assert.match(
    autohideControllerSource,
    /m_animation_translates_content =[\s\S]*?DockAutohideEffect::slide[\s\S]*?autohide_slide_content_offset\([\s\S]*?m_animation_start_horizontal_offset =[\s\S]*?x11_horizontal_offset\(\)[\s\S]*?m_animation_target_horizontal_offset = hiding[\s\S]*?m_animation_start_vertical_offset =[\s\S]*?x11_vertical_offset\(\)[\s\S]*?m_animation_target_vertical_offset = hiding/,
    "native X11 Slide must translate clipped content on every horizontal and vertical edge");
assert.match(
    autohideControllerSource,
    /collapses_x11_horizontally\(\) const[\s\S]*?uses_plasma_x11_edge_effect\(\)[\s\S]*?m_placement\.is_horizontal\(\);/,
    "horizontal X11 collapse must remain exclusive to the Plasma effect");
assert.match(
    autohideControllerSource,
    /uses_plasma_x11_edge_effect\(\) const[\s\S]*?DockAutohideEffect::plasma[\s\S]*?surface_is_native_x11\(\)[\s\S]*?m_has_placement[\s\S]*?collapses_x11_horizontally\([\s\S]*?m_placement\.is_horizontal\(\)[\s\S]*?collapses_x11_vertically\(\) const[\s\S]*?m_placement\.is_vertical\(\)[\s\S]*?m_animation_collapses_horizontally[\s\S]*?m_animation_collapses_vertically[\s\S]*?m_animation_fades = plasma_edge_effect[\s\S]*?SCALE_ANCHOR_CENTER[\s\S]*?m_animation_start_opacity[\s\S]*?m_animation_target_opacity/,
    "the native-X11 Plasma effect must fade and collapse along every edge's main axis");
assert.match(
    dockWindowSource,
    /set_horizontal_scale\([\s\S]*?double anchor\)[\s\S]*?m_horizontal_scale_anchor = clamped_anchor[\s\S]*?set_vertical_scale\([\s\S]*?double anchor\)[\s\S]*?m_vertical_scale_anchor = clamped_anchor[\s\S]*?context->translate\([\s\S]*?m_horizontal_scale_anchor[\s\S]*?m_vertical_scale_anchor[\s\S]*?context->scale\([\s\S]*?m_horizontal_scale,[\s\S]*?m_vertical_scale/,
    "the dock drawing transform must support centred scaling on both axes");
assert.match(
    dockWindowSource,
    /set_horizontal_offset\([\s\S]*?m_horizontal_offset = offset[\s\S]*?context->translate\([\s\S]*?m_horizontal_offset/,
    "the dock drawing transform must support clipped slide translation on vertical edges");
assert.match(
    dockWindowSource,
    /fully_translated[\s\S]*?Cairo::OPERATOR_CLEAR[\s\S]*?context->paint\(\)[\s\S]*?return true;/,
    "a fully hidden slide must clear stale backing pixels before reveal");
assert.match(
    autohideControllerSource,
    /set_placement\([\s\S]*?cancel_animation\(\);\s*reset_local_visual_transform\(\);/,
    "changing dock placement must clear an interrupted X11 transform");
assert.match(
    autohideControllerSource,
    /set_placement\([\s\S]*?preserve_hidden_wayland_surface[\s\S]*?was_hidden &&[\s\S]*?!m_window\.surface_is_native_x11\(\)[\s\S]*?if \(preserve_hidden_wayland_surface\)[\s\S]*?m_reveal_window\.apply_placement\(placement\)[\s\S]*?uses_shell_reveal_trigger\(\)[\s\S]*?finish_surface_autohide_fade\(true\)[\s\S]*?show_reveal_trigger\(\);[\s\S]*?return;[\s\S]*?if \(was_hidden\)[\s\S]*?reveal_immediately\(\)/,
    "Wayland placement changes must preserve autohide instead of exposing the dock during launcher updates");
assert.match(
    autohideControllerSource,
    /set_placement\([\s\S]*?was_hidden &&[\s\S]*?surface_is_native_x11\(\)[\s\S]*?apply_hidden_x11_placement\([\s\S]*?shown_position\);[\s\S]*?m_reveal_window\.apply_placement\(placement\);[\s\S]*?show_reveal_trigger\(\);[\s\S]*?return;[\s\S]*?apply_hidden_x11_placement\([\s\S]*?set_surface_input_passthrough\(true\);[\s\S]*?set_opacity\(0\.0\)[\s\S]*?m_shown_x = shown_position\.x;[\s\S]*?m_shown_y = shown_position\.y;/,
    "native X11 placement changes must rebuild the hidden transform without exposing the dock");
assert.match(
    autohideControllerSource,
    /advance_x11_animation\(\)[\s\S]*?smooth_slide[\s\S]*?DockAutohideEffect::slide[\s\S]*?3\.0 - 2\.0 \* progress[\s\S]*?m_animating_to_hidden[\s\S]*?progress \* progress \* progress[\s\S]*?std::pow\(1\.0 - progress, 3\.0\)/,
    "native X11 Slide must use stable endpoint easing without changing Plasma's established curves");
assert.match(
    autohideControllerSource,
    /!hiding &&[\s\S]*?DockAutohideEffect::slide &&[\s\S]*?m_animation_translates_content[\s\S]*?m_window\.set_opacity\(1\.0\)[\s\S]*?if \(!\(smooth_slide &&[\s\S]*?m_animation_translates_content\)\)/,
    "X11 Slide must stage full opacity while clipped instead of exposing a stale frame beside another dock");
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
    dockWindowControllerSource + tooltipManagerSource,
    /dock_screen_position\(true\)[\s\S]*?TooltipManager::show_now[\s\S]*?m_dock_position\(\)[\s\S]*?show_tooltip\(/,
    "GNOME tooltips must follow the compositor-confirmed dock surface origin");
assert.match(
    dockWindowControllerSource + previewManagerSource,
    /dock_screen_position\(true\)[\s\S]*?PreviewManager::show_now[\s\S]*?m_dock_position\(\)[\s\S]*?show_preview\(/,
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
    /if \(this\._waylandIntegration && this\._isAuxiliaryWindow\(window\)\)\s*this\._beginAuxiliaryTransition\(window, actor\)/,
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
    /\['position-changed', \(\) => \{[\s\S]*?this\._scheduleDockPlacement\(\s*this\._dockTransitioning,[\s\S]*?DOCK_PLACEMENT_DELAY_MS\);/,
    "a steady-state frame-origin change must not begin an opacity-suppressing dock transition");
assert.match(
    extensionSource,
    /\['notify::monitor', \(\) => \{[\s\S]*?this\._scheduleDockPlacement\(\s*this\._dockTransitioning,[\s\S]*?DOCK_PLACEMENT_DELAY_MS\);[\s\S]*?\}\]/,
    "a noisy XWayland monitor notification during resize must not begin an opacity-suppressing dock transition");
assert.match(
    extensionSource,
    /const previousAlignment = this\._dockAlignment;[\s\S]*?const previousLocation = this\._dockLocation;[\s\S]*?const placementPolicyChanged =[\s\S]*?previousAlignment !== this\._dockAlignment[\s\S]*?previousLocation !== this\._dockLocation[\s\S]*?if \(placementPolicyChanged\)\s*this\._beginDockTransition\(\)/,
    "real alignment or edge changes must retain the guarded dock transition");
assert.match(
    extensionSource,
    /const monitorChanged = dockPlacementChangesMonitor\([\s\S]*?this\._dockPlacement = placement;[\s\S]*?if \(monitorChanged\)\s*this\._beginDockTransition\(\);[\s\S]*?this\._scheduleDockPlacement\(true\)/,
    "placement geometry must suppress actor visibility only when it changes monitors");
assert.match(
    extensionSource,
    /const committed = this\._placeDockWindow\(\);[\s\S]*?if \(committed\)[\s\S]*?else if \(\+\+this\._dockPlacementAttempts <[\s\S]*?DOCK_PLACEMENT_MAX_ATTEMPTS\)[\s\S]*?this\._scheduleDockPlacement/,
    "a visible native Wayland resize must keep retrying asynchronous placement without requiring an opacity transition");
assert.match(
    extensionSource,
    /_disconnectBackend\(\) \{[\s\S]*?this\._dockDiscoveredOnce = false/,
    "an app-only restart must rearm early GNOME dock discovery");
assert.match(
    extensionSource,
    /_scheduleDockDiscoveryScan\(\)[\s\S]*?global\.get_window_actors\(\)[\s\S]*?_considerDockWindow\([\s\S]*?false\)/,
    "registration must rescan mapped actors until delayed dock metadata is available");
const dialogTrackingSource = extensionSource.match(
    /_considerDialogWindow\(window\) \{[\s\S]*?return this\._isDockDialog\(window\);\s*\}/)?.[0];
assert.ok(dialogTrackingSource, "GNOME dialog tracking must remain available");
assert.doesNotMatch(
    dialogTrackingSource,
    /get_compositor_private|translation_[xy]|move_frame|\.connect\(/,
    "GNOME dialog placement and movement must remain owned by Mutter");
for (const dialogSource of [
    dockSettingsDialogSource,
    dockAboutDialogSource,
    dockSessionDialogSource,
]) {
    assert.match(
        dialogSource,
        /DesktopSessionIdentity::\s*is_gnome_wayland_session\(\)[\s\S]*?unset_transient_for\(\)/,
        "GNOME Wayland dialogs must detach from an XWayland dock transient");
}
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
    "dockMonitorIndexForRect, dockPlacementChangesMonitor, " +
    "isDockPlacementCommitted, isPointInsideRect, isPointerInsideDockInterior, " +
    "isSyntheticApplicationId, " +
    "parseAuxiliaryPosition, placeDockInWorkArea, rightHideCorridorIntersectsMonitor};";
const context = {};
vm.createContext(context);
vm.runInContext(source, context, {filename: helperPath});

const {
    calculateDockHideOffset,
    calculateDockRevealRect,
    calculateDockStrut,
    clampAuxiliaryToWorkArea,
    dockMonitorIndexForRect,
    dockPlacementChangesMonitor,
    inferDockEdge,
    isDockPlacementCommitted,
    isPointInsideRect,
    isPointerInsideDockInterior,
    isSyntheticApplicationId,
    parseAuxiliaryPosition,
    placeDockInWorkArea,
    rightHideCorridorIntersectsMonitor,
} = context.testApi;
const primary = {x: 0, y: 0, width: 1170, height: 1080};
const primaryWorkArea = {x: 0, y: 32, width: 1170, height: 1048};
const secondary = {x: 1170, y: 164, width: 877, height: 916};

assert.strictEqual(dockMonitorIndexForRect(
    {x: 385, y: 1016, width: 400, height: 64},
    [primary, secondary],
    0), 0);
assert.strictEqual(dockMonitorIndexForRect(
    {x: 1350, y: 1016, width: 400, height: 64},
    [primary, secondary],
    0), 1);
assert.strictEqual(dockPlacementChangesMonitor(
    {x: 385, y: 1016, width: 400, height: 64},
    {x: 417, y: 1016, width: 336, height: 64},
    [primary, secondary],
    0), false);
assert.strictEqual(dockPlacementChangesMonitor(
    null,
    {x: 417, y: 1016, width: 336, height: 64},
    [primary, secondary],
    0), false);
assert.strictEqual(dockPlacementChangesMonitor(
    {x: 385, y: 1016, width: 400, height: 64},
    {x: 1350, y: 1016, width: 400, height: 64},
    [primary, secondary],
    0), true);

assert.strictEqual(rightHideCorridorIntersectsMonitor(
    {x: 1112, y: 32, width: 58, height: 1048, edge: "right"},
    0,
    [primary, {x: 1170, y: 0, width: 1920, height: 1080}]), true);
assert.strictEqual(rightHideCorridorIntersectsMonitor(
    {x: 1112, y: 32, width: 58, height: 1048, edge: "right"},
    0,
    [primary, {x: 1170, y: 1200, width: 1920, height: 1080}]), false);
assert.strictEqual(rightHideCorridorIntersectsMonitor(
    {x: 0, y: 32, width: 58, height: 1048, edge: "left"},
    0,
    [primary, {x: -1920, y: 0, width: 1920, height: 1080}]), false);

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
assert.strictEqual(isPointInsideRect(
    {x: 100, y: 200, width: 300, height: 150}, 100, 200), true);
assert.strictEqual(isPointInsideRect(
    {x: 100, y: 200, width: 300, height: 150}, 399, 349), true);
assert.strictEqual(isPointInsideRect(
    {x: 100, y: 200, width: 300, height: 150}, 400, 349), false);
assert.strictEqual(isPointInsideRect(
    {x: 100, y: 200, width: 300, height: 150}, 399, 350), false);

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
    /in-fullscreen-changed[\s\S]*?_enforceDockWindowLayer\(\)/,
    "GNOME must re-evaluate dock layering when fullscreen state changes");
assert.match(
    extensionSource,
    /_enforceDockWindowLayer\(\)[\s\S]*?get_monitor_in_fullscreen\(monitorIndex\)[\s\S]*?unmake_above\(\)[\s\S]*?make_above\(\)[\s\S]*?stick\(\)/,
    "GNOME must yield the dock to fullscreen windows and restore it above ordinary windows");

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
