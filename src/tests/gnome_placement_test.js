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
const dockTooltipWindowPath = path.resolve(
    __dirname,
    "../dock/dock_tooltip_window.cpp");
const dockWindowPath = path.resolve(
    __dirname,
    "../dock/dock_window.cpp");
const legacySurfaceBackendPath = path.resolve(
    __dirname,
    "../dock/backends/legacy_dock_surface_backend.cpp");
const plasmaSurfaceBackendPath = path.resolve(
    __dirname,
    "../dock/backends/plasma_wayland_dock_surface_backend.cpp");
const dockLayoutTypesPath = path.resolve(
    __dirname,
    "../layout/dock_layout_types.h");
const dockItemPath = path.resolve(
    __dirname,
    "../dock/dock_item.cpp");
const revealWindowPath = path.resolve(
    __dirname,
    "../autohide/dock_reveal_window.cpp");
const previewWindowPath = path.resolve(
    __dirname,
    "../preview/dock_preview_window.cpp");
const thumbnailProviderPath = path.resolve(
    __dirname,
    "../preview/dock_window_thumbnail_provider.cpp");
const windowSystemControllerPath = path.resolve(
    __dirname,
    "../integrations/window_system_controller.cpp");

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
const dockTooltipWindowSource = fs.readFileSync(
    dockTooltipWindowPath, "utf8");
const dockWindowSource = fs.readFileSync(
    dockWindowPath, "utf8");
const legacySurfaceBackendSource = fs.readFileSync(
    legacySurfaceBackendPath, "utf8");
const plasmaSurfaceBackendSource = fs.readFileSync(
    plasmaSurfaceBackendPath, "utf8");
const dockLayoutTypesSource = fs.readFileSync(
    dockLayoutTypesPath, "utf8");
const dockItemSource = fs.readFileSync(
    dockItemPath, "utf8");
const registryChangedHandler = dockWindowControllerSource.match(
    /m_window_registry_changed\s*=[\s\S]*?m_window_geometry_changed\s*=/)?.[0];
const revealWindowSource = fs.readFileSync(
    revealWindowPath, "utf8");
const previewWindowSource = fs.readFileSync(
    previewWindowPath, "utf8");
const thumbnailProviderSource = fs.readFileSync(
    thumbnailProviderPath, "utf8");
const windowSystemControllerSource = fs.readFileSync(
    windowSystemControllerPath, "utf8");

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
    /m_gnome_live_previews_requested = true;[\s\S]*?hide_gnome_live_previews\(\)[\s\S]*?if \(!m_gnome_live_previews_requested\)[\s\S]*?m_gnome_live_previews_requested = false;/,
    "GNOME live-preview teardown must be idempotent");
assert.match(
    thumbnailProviderSource,
    /gnome_shell_capture\s*=\s*normalized_desktop\.find\("gnome"\) != std::string::npos/,
    "GNOME X11 stable window ids must use Shell compositor thumbnail capture");
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
    /GNOME_FALLBACK_CAPTURE_DELAY_MS[\s\S]*?show_gnome_live_previews[\s\S]*?m_gnome_thumbnail_fallback[\s\S]*?generation != m_generation[\s\S]*?!get_visible\(\)[\s\S]*?!entry\.second\.has_thumbnail[\s\S]*?request_thumbnail\([\s\S]*?GNOME_FALLBACK_CAPTURE_DELAY_MS/,
    "a stable GNOME preview must cache a delayed fallback without capturing during transient hover");
assert.match(
    previewWindowSource,
    /GNOME_FALLBACK_CAPTURE_DELAY_MS = 500;/,
    "GNOME fallback capture must start after the preview entrance fade settles");
assert.match(
    previewWindowSource,
    /DockPreviewWindow::show_preview[\s\S]*?cancel_opacity_animation\(\);[\s\S]*?set_opacity\(0\.0\);[\s\S]*?rebuild\(entries, size\)[\s\S]*?m_presentation_pending = true;[\s\S]*?show_all\(\);[\s\S]*?queue_resize\(\);/,
    "a mapped preview must become transparent before its content and geometry are replaced");
assert.match(
    previewWindowSource,
    /signal_size_allocate[\s\S]*?apply_allocated_position\([\s\S]*?complete_presentation\(\)[\s\S]*?DockPreviewWindow::complete_presentation[\s\S]*?start_live_streams\(\);[\s\S]*?start_opacity_animation\(false\);/,
    "preview thumbnails and fade-in must start only after final allocated positioning");
assert.match(
    previewWindowSource,
    /DockPreviewWindow::apply_position[\s\S]*?if \(!m_uses_layer_shell\)[\s\S]*?get_window\(\)[\s\S]*?move_resize\([\s\S]*?global_x,[\s\S]*?global_y,[\s\S]*?width,[\s\S]*?height\)/,
    "an X11 or XWayland preview must move and resize atomically");
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
    dockWindowControllerSource,
    /set_preview_color\([\s\S]*?m_settings\.preview_color\(\)/,
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
    dockWindowControllerSource,
    /if \(reported_workarea && !x11_dock\)/,
    "KWin reports must not replace the authoritative XWayland work area");
assert.match(
    dockWindowControllerSource,
    /if \(!m_window\.surface_uses_native_placement\(\)\)[\s\S]*?apply_workarea_insets/,
    "layer-shell placement must not count compositor work-area insets twice");
assert.match(
    plasmaSurfaceBackendSource,
    /GDK_IS_WAYLAND_DISPLAY[\s\S]*?is_kde_wayland_session\(\)[\s\S]*?gtk_layer_is_supported\(\)[\s\S]*?PlasmaWaylandDockSurfaceBackend/,
    "the Plasma surface backend requires an actual native Wayland display and layer-shell support");
assert.match(
    plasmaSurfaceBackendSource,
    /return std::make_unique<[\s\S]*?LegacyDockSurfaceBackend/,
    "X11 and ordinary Wayland must remain on the legacy surface backend");
assert.doesNotMatch(
    dockWindowSource,
    /gtk_layer_(?:init_for_window|set_monitor|set_anchor|set_margin|set_exclusive_zone|auto_exclusive_zone_enable)/,
    "DockWindow must delegate native Plasma layer-surface operations");
assert.doesNotMatch(
    dockWindowSource,
    /(?:PlasmaWayland|Legacy)DockSurfaceBackend|GDK_IS_WAYLAND_DISPLAY|XDG_CURRENT_DESKTOP/,
    "DockWindow must not select or identify platform surface implementations");
assert.doesNotMatch(
    dockWindowControllerSource,
    /gtk_layer_set_monitor/,
    "DockWindowController must delegate main-surface monitor assignment");
assert.doesNotMatch(
    legacySurfaceBackendSource,
    /gtk_layer_/,
    "the legacy X11 and ordinary-Wayland backend must not apply Plasma layer-surface operations");
for (const operation of [
    "gtk_layer_init_for_window",
    "gtk_layer_set_monitor",
    "gtk_layer_set_anchor",
    "gtk_layer_set_margin",
    "gtk_layer_set_exclusive_zone",
    "gtk_layer_auto_exclusive_zone_enable"
]) {
    assert.ok(
        plasmaSurfaceBackendSource.includes(operation),
        `the Plasma surface backend must own ${operation}`);
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
    /GnomeWaylandWindowBackend>\(\s*!x11\s*\)/,
    "native GNOME X11 must not advertise the Shell-only dock reveal trigger");
assert.match(
    dockWindowControllerSource,
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
    dockWindowControllerSource,
    /m_hovered_item == &item &&[\s\S]*?m_pending_item == &item[\s\S]*?m_tooltip_item == &item/,
    "a stale hovered-item pointer must not suppress a new tooltip request");
assert.match(
    dockItemSource,
    /DockItem::on_enter_notify_event[\s\S]*?schedule_show_tooltip[\s\S]*?DockItem::on_leave_notify_event[\s\S]*?schedule_hide_tooltip/,
    "each item must start and cancel tooltip timing from its own crossing events");
assert.match(
    dockWindowControllerSource,
    /DockWindowController::schedule_show_tooltip[\s\S]*?hide_tooltip\(\);[\s\S]*?start_tooltip_show_timer\(item, text\)/,
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
    dockWindowControllerSource,
    /DockWindowController::schedule_show_preview[\s\S]*?hide_tooltip\(\);[\s\S]*?start_tooltip_show_timer\([\s\S]*?item\.tooltip_text\(\),[\s\S]*?true\);[\s\S]*?m_preview_show_timer/,
    "grouped items must preserve the tooltip hide and delayed reveal effects before preview");
assert.match(
    dockItemSource,
    /signal_button_press_event[\s\S]*?GDK_BUTTON_SECONDARY[\s\S]*?outside_menu[\s\S]*?m_context_menu\.popdown\(\)/,
    "the context menu must catch an outside secondary press consumed by its pointer grab");
assert.match(
    dockItemSource,
    /signal_unmap[\s\S]*?m_context_menu_secondary_dismissed[\s\S]*?schedule_show_tooltip[\s\S]*?uninhibit_autohide\(true\)/,
    "a secondary-button menu dismissal must restore preview and pointer-inside autohide state");
assert.match(
    dockWindowControllerSource,
    /DockWindowController::schedule_show_preview[\s\S]*?start_tooltip_show_timer\([\s\S]*?item\.tooltip_text\(\)[\s\S]*?m_preview_show_timer/,
    "grouped dock items must show a delayed label before their preview");
assert.match(
    dockWindowControllerSource,
    /m_settings\.preview_show_delay\(\) \+[\s\S]*?TOOLTIP_SHOW_DELAY_MS[\s\S]*?TOOLTIP_REMAP_DELAY_MS[\s\S]*?TOOLTIP_FADE_DURATION_MS/,
    "the preview delay must begin after the grouped-item tooltip is fully visible");
assert.match(
    dockWindowControllerSource,
    /DockWindowController::hide_tooltip_immediately[\s\S]*?hide_preview\(\)[\s\S]*?hide_preview_immediately\(\)/,
    "immediate preview closure must clear controller state before hiding the surface");
assert.match(
    revealWindowSource,
    /DockRevealWindow::set_monitor[\s\S]*?m_monitor_geometry[\s\S]*?m_has_placement[\s\S]*?apply_x11_placement\(\)/,
    "an X11 reveal strip must reapply its placement after its monitor geometry changes");
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
    plasmaSurfaceBackendSource,
    /default_autohide_effect\(\) const[\s\S]*?DockAutohideEffect::plasma/,
    "Plasma Wayland must retain its current surface effect");
assert.match(
    legacySurfaceBackendSource,
    /default_autohide_effect\(\) const[\s\S]*?uses_gnome_wayland_autohide_effect\(\)[\s\S]*?DockAutohideEffect::gnome[\s\S]*?DockAutohideEffect::slide/,
    "GNOME Wayland must retain its Shell effect while other legacy surfaces retain the X11 slide");
assert.match(
    dockWindowControllerSource,
    /set_effect\([\s\S]*?surface_default_autohide_effect\(\)[\s\S]*?m_autohide_controller->initialize\(\)/,
    "the shared autohide controller must own the selected backend default");
assert.match(
    autohideControllerSource,
    /can_animate_x11\(\) const[\s\S]*?m_effect == DockAutohideEffect::slide[\s\S]*?m_effect == DockAutohideEffect::gnome[\s\S]*?!has_shell_reveal_trigger\(\)[\s\S]*?surface_is_native_x11\(\)/,
    "native X11 must retain its slide, including the safe GNOME fallback without Shell integration");
assert.match(
    autohideControllerSource,
    /uses_shell_reveal_trigger\(\) const[\s\S]*?surface_delegates_autohide_effect\([\s\S]*?m_effect[\s\S]*?has_shell_reveal_trigger\(\)/,
    "compositor-owned effects must remain routed through the active surface backend and Shell integration");
assert.match(
    dockWindowControllerSource,
    /autohide_effect\(\)\.value_or\([\s\S]*?surface_default_autohide_effect\(\)/,
    "an empty effect setting must preserve the active backend default");
assert.match(
    autohideControllerSource,
    /case DockAutohideEffect::fade:[\s\S]*?animate_fade\(hiding\)/,
    "fade must have an explicit visual-transition dispatch");
assert.match(
    autohideControllerSource,
    /animate_fade\([\s\S]*?cancel_animation\(\)[\s\S]*?surface_autohide_fade_opacity\(\)[\s\S]*?advance_fade_animation[\s\S]*?set_surface_input_passthrough\(true\)[\s\S]*?finish_surface_autohide_fade\(true\)/,
    "local fade must reverse from current opacity and preserve each backend's final hidden state");
assert.match(
    legacySurfaceBackendSource,
    /delegates_autohide_effect\([\s\S]*?uses_gnome_wayland_autohide_effect\(\)[\s\S]*?DockAutohideEffect::fade[\s\S]*?finish_autohide_fade\([\s\S]*?!m_native_x11[\s\S]*?m_window\.hide\(\)/,
    "the legacy backend must delegate GNOME fade while retaining X11's mapped hidden surface");
assert.match(
    plasmaSurfaceBackendSource,
    /set_autohide_fade_opacity\([\s\S]*?m_window\.set_opacity\(opacity\)[\s\S]*?finish_autohide_fade\([\s\S]*?m_window\.hide\(\)/,
    "Plasma fade must use layer-surface opacity before its existing unmapped hidden state");
assert.match(
    extensionSource,
    /get_string\([\s\S]*?'dock', 'autohide_effect'[\s\S]*?configuredEffect === 'fade'[\s\S]*?this\._dockAutohideEffect = autohideEffect/,
    "GNOME must consume the same fade selection as the application");
assert.match(
    extensionSource,
    /_dockAutohideEffect === 'fade'[\s\S]*?_startDockFadeTransition\(hidden, actor\)[\s\S]*?_startDockFadeTransition\(hidden, actor\)[\s\S]*?actor\.remove_all_transitions\(\)[\s\S]*?const startOpacity = actor\.get_opacity\(\)[\s\S]*?actor\.ease\(\{[\s\S]*?opacity: targetOpacity/,
    "GNOME fade must use its compositor actor and reverse from the current opacity");
assert.match(
    autohideControllerSource,
    /finish_shell_animation\([\s\S]*?hidden[\s\S]*?ShellDockState::hidden[\s\S]*?set_surface_input_passthrough\(true\)/,
    "input pass-through must begin only after Shell completes the hide animation");
assert.match(
    extensionSource,
    /_startDockVisibilityTransition\(hidden[\s\S]*?calculateDockHideOffset\(positioned\)[\s\S]*?_updateDockAnimationClip\(actor, positioned, base\)[\s\S]*?EASE_OUT_QUAD[\s\S]*?actor\.ease\(/,
    "GNOME must use Cinnamon-style compositor movement and monitor clipping");
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
    /if \(m_animating_to_hidden\)[\s\S]*?m_window\.set_opacity\(0\.0\);[\s\S]*?set_surface_input_passthrough\(true\);[\s\S]*?else[\s\S]*?reset_x11_visual_transform\(\);/,
    "a hidden native X11 dock must remain mapped and input-pass-through so reveal does not trigger a compositor map effect");
assert.match(
    autohideControllerSource,
    /should_collapse_x11_horizontally\(\)[\s\S]*?horizontal_hide_corridor_intersects_monitor\([\s\S]*?m_animation_collapses_horizontally[\s\S]*?set_x11_horizontal_scale\([\s\S]*?0\.0,[\s\S]*?m_animation_target_scale/,
    "a native X11 vertical dock facing another monitor must collapse at its fixed edge");
assert.match(
    autohideControllerSource,
    /m_animation_collapses_horizontally\s*=\s*m_placement\.is_vertical\(\)\s*&&\s*\(should_collapse_x11_horizontally\(\)\s*\|\|\s*m_window\.x11_horizontal_scale\(\) < 1\.0\)/,
    "a partial vertical collapse must not leak into a horizontal dock edge");
assert.match(
    autohideControllerSource,
    /set_placement\([\s\S]*?cancel_animation\(\);\s*reset_x11_visual_transform\(\);/,
    "changing dock placement must clear an interrupted X11 transform");
assert.match(
    autohideControllerSource,
    /const double eased = m_animating_to_hidden[\s\S]*?progress \* progress \* progress[\s\S]*?1\.0 - std::pow\(1\.0 - progress, 3\.0\)/,
    "every native X11 edge must use the standard hide and reveal curves");
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
    "parseAuxiliaryPosition, placeDockInWorkArea, rightHideCorridorIntersectsMonitor};";
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
    rightHideCorridorIntersectsMonitor,
} = context.testApi;
const primary = {x: 0, y: 0, width: 1170, height: 1080};
const primaryWorkArea = {x: 0, y: 32, width: 1170, height: 1048};

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
