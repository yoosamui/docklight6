import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Clutter from 'gi://Clutter';
import Meta from 'gi://Meta';
import Shell from 'gi://Shell';
import St from 'gi://St';

import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

Gio._promisify(Shell.Screenshot, 'composite_to_stream');

import {
    calculateDockHideOffset,
    calculateDockRevealRect,
    calculateDockStrut,
    clampAuxiliaryToWorkArea,
    dockMonitorIndexForRect,
    dockPlacementChangesMonitor,
    isDockPlacementCommitted,
    isPointInsideRect,
    isPointerInsideDockInterior,
    isSyntheticApplicationId,
    parseAuxiliaryPosition,
    placeDockInWorkArea,
    rightHideCorridorIntersectsMonitor,
} from './placement.js';

const SERVICE = 'org.docklight6.WindowIntegration';
const PATH = '/org/docklight6/WindowIntegration';
const IFACE = 'org.docklight6.WindowIntegration1';
const THUMBNAIL_SERVICE = 'org.docklight6.GnomeThumbnailer';
const THUMBNAIL_PATH = '/org/docklight6/GnomeThumbnailer';
const THUMBNAIL_IFACE = `
<node>
  <interface name="org.docklight6.GnomeThumbnailer1">
    <method name="CaptureWindow">
      <arg type="s" direction="in" name="window_id"/>
      <arg type="i" direction="in" name="target_width"/>
      <arg type="i" direction="in" name="target_height"/>
      <arg type="ay" direction="out" name="png"/>
    </method>
    <method name="ShowLivePreviews">
      <arg type="a(siiii)" direction="in" name="previews"/>
    </method>
    <method name="HoldLivePreviewSurface"/>
    <method name="SetPreviewColor">
      <arg type="d" direction="in" name="red"/>
      <arg type="d" direction="in" name="green"/>
      <arg type="d" direction="in" name="blue"/>
      <arg type="d" direction="in" name="alpha"/>
    </method>
    <method name="ForwardPreviewPrimaryClick">
      <arg type="s" direction="in" name="window_id"/>
      <arg type="d" direction="in" name="normalized_x"/>
      <arg type="d" direction="in" name="normalized_y"/>
    </method>
    <method name="HideLivePreviews"/>
  </interface>
</node>`;
const PROTOCOL_VERSION = '9';
const DOCK_PLACEMENT_DELAY_MS = 30;
// Docklight debounces configuration reloads for 200 ms. Keep the ordinary
// Wayland toplevel hidden past that boundary so an edge change cannot expose
// the old orientation before GTK supplies the final allocation.
const DOCK_TRANSITION_DELAY_MS = 300;
const DOCK_PLACEMENT_MAX_ATTEMPTS = 40;
const DOCK_DISCOVERY_MAX_ATTEMPTS = 30;
const REGISTRATION_RETRY_MS = 250;
const CONFIGURATION_SETTLE_MS = 50;
// Keep GNOME's compositor-owned effects aligned with Docklight's 200 ms
// Plasma-style and slide/fade transitions.
const DOCK_HIDE_ANIMATION_MS = 200;
const DOCK_REVEAL_ANIMATION_MS = 200;
const DOCK_CLIP_FRAME_MS = 16;
// Browser PiP surfaces commonly reserve a double-click for maximizing the
// player. The preview bridge must not turn a stress-click burst into that
// window-management gesture.
const PREVIEW_DOUBLE_CLICK_GUARD_US = 500000;
const PREVIEW_DOUBLE_CLICK_DISTANCE_PX = 12;
// Match the GTK tooltip's subtle centred fade/scale on GNOME Wayland.
const PREVIEW_VISIBILITY_ANIMATION_MS = 180;
const PREVIEW_VISIBILITY_MIN_SCALE = 0.96;
const PREVIEW_VISIBILITY_INITIAL_OPACITY = 46;
const PREVIEW_CLONE_FADE_MS = 100;
const PREVIEW_REPLACEMENT_REVEAL_DELAY_MS = 50;
const PREVIEW_REPLACEMENT_CROSSFADE_MS = 100;

const TRACKABLE_TYPES = new Set([
    Meta.WindowType.NORMAL,
    Meta.WindowType.DIALOG,
    Meta.WindowType.MODAL_DIALOG,
    Meta.WindowType.UTILITY,
]);

function encodeList(values) {
    return values
        .filter(value => value !== null && value !== undefined)
        .map(value => encodeURIComponent(String(value)))
        .join(',');
}

function decodeList(value) {
    return value ? value.split(',').map(decodeURIComponent) : [];
}

function booleanText(value) {
    return value ? '1' : '0';
}

function integerText(value) {
    const number = Number(value);
    return Number.isFinite(number) ? String(Math.round(number)) : '0';
}

export default class DocklightWindowIntegration extends Extension {
    enable() {
        this._enabled = true;
        this._waylandIntegration = Meta.is_wayland_compositor();

        this._proxy = Gio.DBusProxy.new_for_bus_sync(
            Gio.BusType.SESSION,
            Gio.DBusProxyFlags.DO_NOT_LOAD_PROPERTIES,
            null,
            SERVICE,
            PATH,
            IFACE,
            null);

        this._tracker = Shell.WindowTracker.get_default();
        this._windows = new Map();
        this._windowSignals = new Map();
        this._iconGeometries = new Map();
        this._dockHidden = false;
        this._signals = [];
        this._connect(this._proxy, 'g-signal',
            (_proxy, _sender, signalName, parameters) => {
                if (signalName === 'IconGeometryChanged')
                    this._setIconGeometry(...parameters.deepUnpack());
                else if (signalName === 'IconGeometryRemoved')
                    this._removeIconGeometry(parameters.deepUnpack()?.[0]);
                else if (signalName === 'DockPlacementGeometryChanged')
                    this._setDockPlacement(parameters.deepUnpack());
                else if (signalName === 'DockHiddenChanged') {
                    this._dockHidden = Boolean(parameters.deepUnpack()?.[0]);
                    this._startDockVisibilityTransition(this._dockHidden);
                }
            });
        this._revision = 0;
        this._connected = false;
        this._registering = false;
        this._serviceAvailable = false;
        this._pendingWaits = 0;
        this._registrationRetrySource = 0;
        this._dockWindow = null;
        // The application-id fallback is safe for the first dock surface in
        // one application lifetime. It is rearmed when the application's
        // D-Bus name vanishes so an app-only restart gets the same guarded
        // early placement as the original launch.
        this._dockDiscoveredOnce = false;
        this._dockPlacement = null;
        this._dockWindowSignals = [];
        this._dockPlacementSource = 0;
        this._dockPlacementAttempts = 0;
        this._dockDiscoverySources = new Set();
        this._dockDiscoveryScanSource = 0;
        this._dockTransitioning = false;
        this._dockActor = null;
        this._dockActorOpacity = 255;
        this._dockActorBaseTranslation = {x: 0, y: 0};
        this._dockVisibilityAnimationSerial = 0;
        this._dockClipSource = 0;
        this._dockVisibilityState = 'visible';
        this._dockPointerInside = null;
        this._pointerPosition = null;
        this._cursorTracker = global.backend.get_cursor_tracker();
        this._pointerPollSource = 0;
        this._auxiliaryWindowSignals = new Map();
        this._auxiliaryTransitions = new Map();
        this._configurationReloadSource = 0;
        this._dockStrut = null;
        this._dockRevealActor = null;
        this._dockRevealSignal = 0;
        this._nativeWorkAreas = [];
        this._livePreviewOverlay = null;
        this._previewSessionOpen = false;
        this._previewReplacementShield = null;
        this._previewReplacementShieldReleaseSource = 0;
        this._livePreviewRects = [];
        this._previewPointerInside = null;
        this._previewPointerDevice = null;
        this._previewPointerSources = new Set();
        this._previewPointerPressed = false;
        this._previewPointerRestorePosition = null;
        this._previewPointerLastClick = null;
        this._previewInputSuppressedActors = [];
        this._previewSelectorFill =
            'rgba(105, 170, 255, 0.32)';
        this._previewSelectorOutline =
            'rgba(105, 170, 255, 0.95)';

        this._thumbnailDbus = null;
        this._thumbnailNameId = 0;
        if (this._waylandIntegration) {
            this._thumbnailDbus = Gio.DBusExportedObject.wrapJSObject(
                THUMBNAIL_IFACE, this);
            this._thumbnailDbus.export(Gio.DBus.session, THUMBNAIL_PATH);
            this._thumbnailNameId = Gio.bus_own_name_on_connection(
                Gio.DBus.session,
                THUMBNAIL_SERVICE,
                Gio.BusNameOwnerFlags.NONE,
                null,
                null);
        }

        this._loadDockPlacement();
        this._refreshNativeWorkAreas();
        this._watchDockConfiguration();
        this._pointerPollSource = 0;
        if (this._waylandIntegration) {
            this._ensureDockRevealActor();
            this._updateDockRevealActor();
            this._pointerPollSource = GLib.timeout_add(
                GLib.PRIORITY_DEFAULT,
                25,
                () => {
                    const [position] = this._cursorTracker.get_pointer();
                    const restore = this._previewPointerRestorePosition;
                    if (restore) {
                        if (restore.restoring) {
                            const restored =
                                Math.abs(position.x - restore.x) <= 1 &&
                                Math.abs(position.y - restore.y) <= 1;
                            const expired = GLib.get_monotonic_time() >=
                                restore.deadline;
                            if (restored || expired) {
                                this._previewPointerRestorePosition = null;
                                this._pointerPosition = {
                                    x: position.x,
                                    y: position.y,
                                };
                            }
                        }
                    } else {
                        this._pointerPosition = {x: position.x, y: position.y};
                    }
                    this._publishDockPointerInside();
                    this._publishPreviewPointerInside();
                    return GLib.SOURCE_CONTINUE;
                });
        }

        this._connect(global.display, 'window-created', (_display, window) => {
            if (this._waylandIntegration)
                this._onWindowAdded(window);
            else
                this._considerDockWindow(window);
        });
        this._connect(global.window_manager, 'map', (_windowManager, actor) => {
            const window = actor?.meta_window;
            if (!window)
                return;

            // Auxiliary GTK toplevels are subject to Mutter's provisional
            // centred placement too. Hide their first actor before any
            // classification work can expose it.
            if (this._waylandIntegration && this._isAuxiliaryWindow(window))
                this._beginAuxiliaryTransition(window, actor);

            // window-created can precede the compositor actor. Reconsider the
            // dock at map time, when opacity can still suppress Mutter's
            // provisional centred frame before it reaches the screen.
            this._considerDockWindow(window);
            if (this._dockWindow === window) {
                this._dockActor = actor;
                this._beginDockTransition();
                this._scheduleDockPlacement(true);
            }
        });
        if (this._waylandIntegration) {
            this._connect(global.display, 'window-demands-attention',
                (_display, window) => this._publishWindow(window));
            this._connect(global.display, 'in-fullscreen-changed', () => {
                this._enforceDockWindowLayer();
            });
            this._connect(global.display, 'notify::focus-window', () => {
                this._publishActiveWindow();
            });
            this._connect(global.display, 'restacked', () => {
                this._enforceDockWindowLayer();
                this._publishStackingOrder();
            });
            this._connect(global.workspace_manager,
                'active-workspace-changed', () => {
                    this._publishCurrentDesktop();
                    this._publishAllWindows();
                });
        }
        this._connect(Main.layoutManager, 'monitors-changed', () => {
            this._removeDockStrut();
            this._refreshNativeWorkAreas();
            this._updateDockRevealActor();
            this._beginDockTransition();
            this._scheduleDockPlacement(true);
        });

        for (const actor of global.get_window_actors()) {
            const window = actor.meta_window;
            this._considerDockWindow(window);
            if (this._waylandIntegration)
                this._trackWindow(window);
        }

        this._nameWatch = Gio.bus_watch_name(
            Gio.BusType.SESSION,
            SERVICE,
            Gio.BusNameWatcherFlags.NONE,
            () => {
                this._serviceAvailable = true;
                this._register();
            },
            () => {
                this._serviceAvailable = false;
                this._disconnectBackend();
            });
    }

    disable() {
        if (!this._enabled)
            return;

        this._enabled = false;
        this._connected = false;

        if (this._thumbnailNameId) {
            Gio.bus_unown_name(this._thumbnailNameId);
            this._thumbnailNameId = 0;
        }
        if (this._thumbnailDbus) {
            this._thumbnailDbus.unexport();
            this._thumbnailDbus = null;
        }

        if (this._registrationRetrySource) {
            GLib.source_remove(this._registrationRetrySource);
            this._registrationRetrySource = 0;
        }

        if (this._dockPlacementSource) {
            GLib.source_remove(this._dockPlacementSource);
            this._dockPlacementSource = 0;
        }
        if (this._pointerPollSource) {
            GLib.source_remove(this._pointerPollSource);
            this._pointerPollSource = 0;
        }
        this._dockVisibilityAnimationSerial++;
        this._cancelDockAnimationClip();
        if (this._configurationReloadSource) {
            GLib.source_remove(this._configurationReloadSource);
            this._configurationReloadSource = 0;
        }
        for (const source of this._dockDiscoverySources)
            GLib.source_remove(source);
        this._dockDiscoverySources.clear();
        if (this._dockDiscoveryScanSource) {
            GLib.source_remove(this._dockDiscoveryScanSource);
            this._dockDiscoveryScanSource = 0;
        }
        this._clearDockWindow();
        this._destroyLivePreviews();
        this._cancelPreviewPointerInput(true);
        this._removeDockStrut();
        this._destroyDockRevealActor();
        for (const window of [...this._auxiliaryWindowSignals.keys()])
            this._clearAuxiliaryWindow(window);

        if (this._configurationMonitor) {
            this._configurationMonitor.cancel();
            this._configurationMonitor = null;
        }

        if (this._nameWatch) {
            Gio.bus_unwatch_name(this._nameWatch);
            this._nameWatch = 0;
        }

        for (const [object, id] of this._signals)
            object.disconnect(id);
        this._signals = [];

        this._clearIconGeometries();

        for (const window of [...this._windowSignals.keys()])
            this._untrackWindow(window);

        if (this._proxy) {
            this._call('Unregister', null, null, () => { });
            this._proxy = null;
        }

        this._windows = null;
        this._windowSignals = null;
        this._iconGeometries = null;
        this._tracker = null;
        this._cursorTracker = null;
    }

    _loadDockPlacement() {
        let autohide = 'none';
        let autohideEffect = 'gnome';
        let alignment = 'center';
        let location = 'bottom';

        const path = GLib.build_filenamev([
            GLib.get_user_config_dir(), 'docklight6', 'docklight.conf',
        ]);
        const keyFile = new GLib.KeyFile();

        try {
            keyFile.load_from_file(path, GLib.KeyFileFlags.NONE);
            try {
                const configuredAutohide = keyFile.get_string('dock', 'autohide').trim();
                if (['none', 'autohide', 'intellihide'].includes(configuredAutohide))
                    autohide = configuredAutohide;
            } catch (_error) {
                // Older configuration files use the non-hiding default.
            }
            try {
                const configuredEffect = keyFile.get_string(
                    'dock', 'autohide_effect').trim();
                if (['fade', 'slide_fade'].includes(configuredEffect))
                    autohideEffect = configuredEffect;
            } catch (_error) {
                // Missing and empty values retain the GNOME effect.
            }
            try {
                const configuredAlignment = keyFile.get_string('dock', 'alignment').trim();
                if (['start', 'center', 'end', 'fill'].includes(configuredAlignment))
                    alignment = configuredAlignment;
            } catch (_error) {
                // Missing and empty alignment values use the centred default.
            }
            try {
                const configuredLocation = keyFile.get_string('dock', 'location').trim();
                if (['top', 'bottom', 'left', 'right'].includes(configuredLocation))
                    location = configuredLocation;
            } catch (_error) {
                // Missing and empty location values use the bottom edge.
            }
        } catch (_error) {
            // Missing keys and a missing first-run file both mean defaults.
        }

        const changed = this._dockAutohide !== autohide ||
            this._dockAutohideEffect !== autohideEffect ||
            this._dockAlignment !== alignment ||
            this._dockLocation !== location;
        this._dockAutohide = autohide;
        this._dockAutohideEffect = autohideEffect;
        this._dockAlignment = alignment;
        this._dockLocation = location;
        return changed;
    }

    _watchDockConfiguration() {
        const directory = Gio.File.new_for_path(GLib.build_filenamev([
            GLib.get_user_config_dir(), 'docklight6',
        ]));

        try {
            this._configurationMonitor = directory.monitor_directory(
                Gio.FileMonitorFlags.NONE, null);
            this._configurationMonitor.connect('changed', (_monitor, file) => {
                if (file?.get_basename() !== 'docklight.conf')
                    return;
                if (this._configurationReloadSource)
                    GLib.source_remove(this._configurationReloadSource);
                this._configurationReloadSource = GLib.timeout_add(
                    GLib.PRIORITY_DEFAULT,
                    CONFIGURATION_SETTLE_MS,
                    () => {
                        this._configurationReloadSource = 0;
                        const previousAlignment = this._dockAlignment;
                        const previousLocation = this._dockLocation;
                        if (this._loadDockPlacement()) {
                            const placementPolicyChanged =
                                previousAlignment !== this._dockAlignment ||
                                previousLocation !== this._dockLocation;
                            if (placementPolicyChanged)
                                this._beginDockTransition();
                            this._scheduleDockPlacement(
                                placementPolicyChanged);
                            this._updateDockRevealActor();
                        }
                        return GLib.SOURCE_REMOVE;
                    });
            });
        } catch (_error) {
            this._configurationMonitor = null;
        }
    }

    _isDockWindow(window) {
        if (!window)
            return false;

        let title = '';
        let role = '';
        try {
            title = window.get_title?.() || '';
            role = window.get_role?.() || '';
        } catch (_error) {
            // Window metadata can still be incomplete immediately after map.
        }

        if (title.toLowerCase() === 'docklight 6 dock' ||
            role.toLowerCase() === 'docklight6-dock')
            return true;

        // Native X11 is deliberately animation-only. Never infer its dock
        // from the shared application id: preview, tooltip, reveal, and
        // dialog windows belong entirely to GTK/EWMH and must remain outside
        // the Shell integration even while their metadata is incomplete.
        if (!this._waylandIntegration)
            return false;

        // Explicit private-surface identities always win over the shared GTK
        // application id. In particular, the reveal trigger maps immediately
        // after the dock unmaps and can otherwise steal the dock identity.
        if (parseAuxiliaryPosition(title) ||
            role.toLowerCase() === 'docklight6-reveal')
            return false;

        // GTK's title and role can arrive after Mutter has already placed the
        // first frame. The application id and NORMAL type are available early
        // enough to hide and move the initial dock before that frame is drawn.
        let applicationId = '';
        let type = null;
        try {
            applicationId = window.get_gtk_application_id?.() || '';
            type = window.get_window_type?.();
        } catch (_error) {
            return false;
        }
        return !this._dockWindow &&
            !this._dockDiscoveredOnce &&
            applicationId === 'org.docklight6' &&
            (type === Meta.WindowType.NORMAL ||
                type === Meta.WindowType.DOCK) &&
            !window.get_transient_for?.();
    }

    _considerDockWindow(window, allowRetry = true) {
        if (this._waylandIntegration) {
            if (this._considerDialogWindow(window))
                return;
            if (this._considerAuxiliaryWindow(window))
                return;
        }

        if (this._isDockWindow(window)) {
            this._setDockWindow(window);
            return;
        }

        // Mutter can emit window-created before GTK's title and role have
        // propagated. Retry briefly after mapping, but never infer the dock
        // from the shared application identity: dialogs use that identity too.
        if (!allowRetry)
            return;

        let attempts = 0;
        const source = GLib.timeout_add(GLib.PRIORITY_DEFAULT, 50, () => {
            const isDock = this._isDockWindow(window);
            const isAuxiliary = this._waylandIntegration &&
                this._considerAuxiliaryWindow(window);
            if (!this._enabled || ++attempts > 10 || isDock || isAuxiliary) {
                this._dockDiscoverySources.delete(source);
                if (this._enabled && isDock)
                    this._setDockWindow(window);
                return GLib.SOURCE_REMOVE;
            }
            return GLib.SOURCE_CONTINUE;
        });
        this._dockDiscoverySources.add(source);
    }

    _scheduleDockDiscoveryScan() {
        if (this._dockWindow || this._dockDiscoveryScanSource)
            return;

        let attempts = 0;
        this._dockDiscoveryScanSource = GLib.timeout_add(
            GLib.PRIORITY_DEFAULT,
            100,
            () => {
                for (const actor of global.get_window_actors()) {
                    this._considerDockWindow(
                        actor.meta_window,
                        false);
                    if (this._dockWindow)
                        break;
                }

                if (this._dockWindow ||
                    ++attempts >= DOCK_DISCOVERY_MAX_ATTEMPTS) {
                    this._dockDiscoveryScanSource = 0;
                    return GLib.SOURCE_REMOVE;
                }

                return GLib.SOURCE_CONTINUE;
            });
    }

    _isDockDialog(window) {
        let title = '';
        let role = '';
        let applicationId = '';
        let type = null;
        try {
            title = window?.get_title?.() || '';
            role = window?.get_role?.() || '';
            applicationId = window?.get_gtk_application_id?.() || '';
            type = window?.get_window_type?.();
        } catch (_error) {
            return false;
        }

        // The dock deliberately uses a dock window hint on compositors that
        // lack layer-shell. Some Mutter/GTK combinations expose that surface
        // as a dialog-like type, so its explicit identity must win over the
        // broad application-id fallback used for real settings dialogs.
        if (role.toLowerCase() === 'docklight6-dock' ||
            title.toLowerCase() === 'docklight 6 dock')
            return false;

        return role === 'docklight6-settings' || role === 'docklight6-about' ||
            title === 'DockLight Settings' || title === 'About DockLight' ||
            (applicationId === 'org.docklight6' &&
                (type === Meta.WindowType.DIALOG ||
                    type === Meta.WindowType.MODAL_DIALOG));
    }

    _considerDialogWindow(window) {
        // Settings and About are ordinary unparented Wayland toplevels.
        // Classify them here so the shared application id cannot make them
        // the dock, but leave placement and interactive movement entirely to
        // Mutter. Translating their compositor actors separates the visible
        // input surface from the native frame and breaks mouse move grabs.
        return this._isDockDialog(window);
    }

    _auxiliaryPosition(window) {
        let title = '';
        try {
            title = window?.get_title?.() || '';
        } catch (_error) {
            return null;
        }

        return parseAuxiliaryPosition(title);
    }

    _isAuxiliaryWindow(window) {
        return this._auxiliaryPosition(window) !== null;
    }

    _considerAuxiliaryWindow(window) {
        const position = this._auxiliaryPosition(window);
        if (!position)
            return false;

        // window-created can expose the shared GTK application id before the
        // private-surface title and role arrive. If that provisional metadata
        // made this reveal/tooltip surface look like the dock, undo the dock
        // transition first. In particular this restores actor opacity and
        // prevents the edge trigger from retaining dock placement signals.
        if (this._dockWindow === window)
            this._clearDockWindow();

        // Metadata can arrive after map. Begin the same guarded placement
        // transition here as well so correcting a provisional dock identity
        // cannot briefly restore Mutter's centred actor.
        this._beginAuxiliaryTransition(window);

        if (!this._auxiliaryWindowSignals.has(window)) {
            const signals = [];
            for (const [signal, callback] of [
                ['notify::title', () => this._placeAuxiliaryWindow(window)],
                ['size-changed', () => this._placeAuxiliaryWindow(window)],
                ['position-changed', () => this._placeAuxiliaryWindow(window)],
                ['unmanaged', () => this._clearAuxiliaryWindow(window)],
            ]) {
                try {
                    signals.push(window.connect(signal, callback));
                } catch (_error) {
                    // Signal availability differs between Mutter releases.
                }
            }
            this._auxiliaryWindowSignals.set(window, signals);

            // If metadata arrived late, remove the private Docklight surface
            // from the public application-window snapshot immediately.
            if (this._windows.has(this._windowId(window)))
                this._onWindowRemoved(window);
        }

        this._placeAuxiliaryWindow(window, position);
        return true;
    }

    _placeAuxiliaryWindow(window, position = null) {
        const target = position || this._auxiliaryPosition(window);
        if (!target)
            return;

        const actor = window?.get_compositor_private?.();
        if (!actor)
            return;

        // Tooltips, previews, and the GTK reveal surface are short-lived
        // Wayland toplevels. Meta.Window.move_frame() is unsafe for these
        // surfaces on Mutter 16 and delayed retries can outlive the native
        // window. Translate the compositor actor synchronously instead.
        const rect = window.get_frame_rect();
        const resolvedTarget = target.type === 'reveal'
            ? target
            : clampAuxiliaryToWorkArea(
                target,
                rect,
                this._workAreaForMonitor(this._dockMonitorIndex()));
        const previewVisibilityAnimation =
            target.type === 'preview' && Meta.is_wayland_compositor();
        if (!previewVisibilityAnimation) {
            actor.remove_all_transitions();
            actor.scale_x = 1;
            actor.scale_y = 1;
        }
        actor.translation_x = resolvedTarget.x - rect.x;
        actor.translation_y = resolvedTarget.y - rect.y;

        // KDE's layer-shell backend places private surfaces on OVERLAY and
        // the dock on TOP. GNOME has no layer-shell, so explicitly preserve
        // the GTK auxiliary as sticky and above before raising it within
        // Mutter's TOP stack layer. GTK submits equivalent XWayland hints,
        // but a cross-workspace activation can race those hints and move the
        // card/header actor away while the Shell-owned live-preview overlay
        // remains on screen. That split state loses the visible Preview
        // border and leaves its next click with confusing toggle feedback.
        // Changing WindowActor sibling order is only temporary and is undone
        // by the compositor's next restack, so keep this on Meta.Window.
        try {
            window.make_above();
            window.stick();
            window.raise();
        } catch (_error) {
            // The Meta.Window can disappear while a preview is closing.
        }
        this._finishAuxiliaryTransition(window);
    }

    _beginAuxiliaryTransition(window, actor = null) {
        if (this._auxiliaryTransitions.has(window))
            return;

        const compositorActor = actor ||
            window.get_compositor_private?.();
        if (!compositorActor)
            return;

        const opacity = compositorActor.get_opacity();
        this._auxiliaryTransitions.set(window, {
            actor: compositorActor,
            opacity: opacity > 0 ? opacity : 255,
        });
        compositorActor.remove_all_transitions();
        compositorActor.scale_x = 1;
        compositorActor.scale_y = 1;
        compositorActor.set_opacity(0);
    }

    _finishAuxiliaryTransition(window) {
        const transition = this._auxiliaryTransitions.get(window);
        if (!transition)
            return;

        try {
            const preview = this._auxiliaryPosition(window)?.type === 'preview';
            if (preview && Meta.is_wayland_compositor()) {
                transition.actor.set_pivot_point(0.5, 0.5);

                // Overlay construction is asynchronous relative to GTK's
                // unmap/remap. Keep logical visibility separate from actor
                // lifetime so adjacent DockItems never replay the opening
                // effect merely because the replacement overlay is between
                // its destroy and install steps.
                if (this._previewSessionOpen) {
                    transition.actor.scale_x = 1;
                    transition.actor.scale_y = 1;
                    transition.actor.set_opacity(transition.opacity);
                } else {
                    transition.actor.scale_x =
                        PREVIEW_VISIBILITY_MIN_SCALE;
                    transition.actor.scale_y =
                        PREVIEW_VISIBILITY_MIN_SCALE;
                    transition.actor.set_opacity(
                        PREVIEW_VISIBILITY_INITIAL_OPACITY);
                    transition.actor.ease({
                        scale_x: 1,
                        scale_y: 1,
                        opacity: transition.opacity,
                        duration: PREVIEW_VISIBILITY_ANIMATION_MS,
                        mode: Clutter.AnimationMode.EASE_IN_OUT_QUINT,
                    });
                }
            } else {
                transition.actor.set_opacity(transition.opacity);
            }
        } catch (_error) {
            // The actor can disappear while its GTK window is closing.
        }
        this._auxiliaryTransitions.delete(window);
    }

    _clearAuxiliaryWindow(window) {
        this._finishAuxiliaryTransition(window);
        for (const id of this._auxiliaryWindowSignals.get(window) || []) {
            try {
                window.disconnect(id);
            } catch (_error) {
                // The Meta.Window may already be finalized.
            }
        }
        this._auxiliaryWindowSignals.delete(window);
    }

    _setDockWindow(window) {
        if (this._dockWindow === window) {
            this._scheduleDockPlacement();
            return;
        }

        this._clearDockWindow();
        this._dockWindow = window;
        this._dockDiscoveredOnce = true;
        if (this._waylandIntegration)
            this._enforceDockWindowLayer();
        this._updateDockRevealActor();
        this._beginDockTransition();

        for (const [signal, callback] of [
            ['position-changed', () => {
                // Mutter can apply its ordinary-toplevel initial placement
                // after window-created. Reassert the configured edge when
                // that late placement moves the dock back to the centre. A
                // steady-state XWayland resize can also change the frame
                // origin to preserve centre/end alignment; do not hide the
                // already visible actor for that routine correction.
                this._scheduleDockPlacement(
                    this._dockTransitioning,
                    DOCK_PLACEMENT_DELAY_MS);
            }],
            ['size-changed', () => {
                this._scheduleDockPlacement(this._dockTransitioning);
            }],
            ['notify::monitor', () => {
                // Mutter can notify the monitor property while an XWayland
                // frame is merely resizing on the same output. Placement
                // geometry is the authoritative source for a real configured
                // monitor change, so this noisy signal must not blank the
                // already visible actor.
                this._scheduleDockPlacement(
                    this._dockTransitioning,
                    DOCK_PLACEMENT_DELAY_MS);
            }],
            ['unmanaged', () => this._clearDockWindow(true)],
        ]) {
            try {
                this._dockWindowSignals.push(window.connect(signal, callback));
            } catch (_error) {
                // Signal availability differs slightly between Mutter releases.
            }
        }

        // Wait for GTK's final allocation. The actor remains hidden during
        // this interval, avoiding a visible centred/old-axis frame followed
        // by the configured edge frame.
        this._scheduleDockPlacement(true);
    }

    _enforceDockWindowLayer() {
        if (!this._dockWindow)
            return;

        try {
            const monitorIndex = this._dockMonitorIndex();
            const monitorInFullscreen =
                global.display.get_monitor_in_fullscreen(monitorIndex);

            // Mutter expects desktop controls to yield when a fullscreen
            // window obscures their monitor. The dock normally needs ABOVE
            // because its native Wayland surface is an ordinary toplevel,
            // but retaining that state also puts it over fullscreen clients.
            // Drop only ABOVE while fullscreen is active; keeping the dock
            // mapped preserves GTK's autohide state and makes restoration
            // immediate when Mutter reports that fullscreen has ended.
            if (monitorInFullscreen) {
                if (this._dockWindow.is_above())
                    this._dockWindow.unmake_above();
            } else if (!this._dockWindow.is_above()) {
                this._dockWindow.make_above();
            }
            this._dockWindow.stick();
        } catch (_error) {
            // The dock can be unmanaged while a restack is being delivered.
        }
    }

    _isX11DockWindow(window = this._dockWindow) {
        try {
            return window?.get_client_type?.() ===
                Meta.WindowClientType.X11;
        } catch (_error) {
            return false;
        }
    }

    _clearDockWindow(unmanaged = false) {
        // The unmanaged signal is emitted while Mutter is disposing the
        // compositor actor. Do not restore or otherwise dereference it; the
        // actor owns and discards its transitions and clip during disposal.
        this._cancelDockAnimationClip(!unmanaged);
        if (!unmanaged && !this._waylandIntegration)
            this._restoreX11DockActor();
        if (unmanaged) {
            this._dockTransitioning = false;
            this._dockPlacementAttempts = 0;
            this._dockActor = null;
            this._dockActorOpacity = 255;
        } else {
            this._finishDockTransition();
        }
        this._removeDockStrut();

        if (this._dockWindow)
            this._publishDockSurfaceGeometry(null);

        if (this._dockWindow) {
            for (const id of this._dockWindowSignals) {
                try {
                    this._dockWindow.disconnect(id);
                } catch (_error) {
                    // The Meta.Window may already be finalized.
                }
            }
        }
        this._dockWindowSignals = [];
        this._dockWindow = null;
        this._dockActor = null;
        this._updateDockRevealActor();
    }

    _restoreX11DockActor() {
        if (this._waylandIntegration)
            return;

        const actor = this._dockWindow?.get_compositor_private?.();
        if (!actor)
            return;

        try {
            this._dockVisibilityAnimationSerial++;
            actor.remove_all_transitions();
            actor.remove_clip();
            actor.translation_x = 0;
            actor.translation_y = 0;
            actor.scale_x = 1;
            actor.scale_y = 1;
            actor.set_pivot_point(0.5, 0.5);
            actor.set_opacity(this._dockActorOpacity || 255);
        } catch (_error) {
            // Mutter can dispose the actor while the X11 client is closing.
        }
        this._dockVisibilityState = 'visible';
    }

    _beginDockTransition() {
        if (!this._dockWindow)
            return;

        // Native X11 placement and initial-map visibility remain owned by
        // GTK/EWMH. Its later autohide transitions may still animate this
        // actor through the narrow animation-only bridge.
        if (!Meta.is_wayland_compositor()) {
            this._dockTransitioning = false;
            this._dockActor = null;
            return;
        }

        if (!this._dockTransitioning) {
            this._dockTransitioning = true;
            this._dockActorOpacity = 255;
            this._dockPlacementAttempts = 0;
        }

        const actor = this._dockWindow.get_compositor_private?.();
        if (!actor)
            return;

        if (!this._dockActor) {
            this._dockActor = actor;
            const opacity = actor.get_opacity();
            this._dockActorOpacity = opacity > 0 ? opacity : 255;
        }

        // Mutter initially places an ordinary Wayland toplevel at the screen
        // centre. Keep that provisional frame (and an old-axis frame during
        // an edge change) out of the scene until its final size is available.
        // GNOME's normal-window map animation is already attached when this
        // extension receives the map signal; cancel it so it cannot restore
        // opacity or scale the provisional centred actor into view.
        actor.remove_all_transitions();
        actor.scale_x = 1;
        actor.scale_y = 1;
        actor.set_opacity(0);
    }

    _finishDockTransition() {
        if (this._dockTransitioning && this._dockActor) {
            try {
                // Autohide can change state while asynchronous Wayland
                // placement is settling. Finish at the current state instead
                // of replaying a reveal over a hide that already completed.
                this._startDockVisibilityTransition(
                    this._dockHidden, this._dockActor);
            } catch (_error) {
                // The actor can disappear while the window is being closed.
            }
        }

        this._dockTransitioning = false;
        this._dockPlacementAttempts = 0;
        this._dockActor = null;
        this._dockActorOpacity = 255;
    }

    _cancelDockAnimationClip(removeClip = true) {
        if (this._dockClipSource) {
            GLib.source_remove(this._dockClipSource);
            this._dockClipSource = 0;
        }

        if (removeClip) {
            try {
                this._dockWindow?.get_compositor_private?.()?.remove_clip();
            } catch (_error) {
                // The actor can disappear while its window is being closed.
            }
        }
    }

    _updateDockAnimationClip(actor, positioned, base) {
        if (!this._isX11DockWindow() || !actor || !positioned)
            return;

        const monitor =
            Main.layoutManager.monitors[this._dockMonitorIndex()];
        if (!monitor)
            return;

        const actorX = positioned.x + actor.translation_x - base.x;
        const actorY = positioned.y + actor.translation_y - base.y;
        const left = Math.max(actorX, monitor.x);
        const top = Math.max(actorY, monitor.y);
        const right = Math.min(
            actorX + positioned.width,
            monitor.x + monitor.width);
        const bottom = Math.min(
            actorY + positioned.height,
            monitor.y + monitor.height);

        actor.set_clip(
            Math.max(0, Math.round(left - actorX)),
            Math.max(0, Math.round(top - actorY)),
            Math.max(0, Math.round(right - left)),
            Math.max(0, Math.round(bottom - top)));
    }

    _startDockVisibilityTransition(hidden, requestedActor = null) {
        const actor = requestedActor ??
            this._dockWindow?.get_compositor_private?.();
        const placement = this._dockPlacement;
        if (!actor || !placement) {
            this._dockVisibilityState = hidden ? 'hidden' : 'visible';
            this._updateDockRevealActor();
            this._call('PublishDockAnimationCompleted', '(b)', [hidden]);
            return;
        }

        if (this._dockAutohideEffect === 'fade') {
            this._startDockFadeTransition(hidden, actor);
            return;
        }

        if (this._dockAutohideEffect !== 'slide_fade') {
            this._startDockPlasmaStyleTransition(hidden, actor);
            return;
        }

        // GNOME owns the compositor actor, so reproduce Plasma's normalized
        // outward movement and opacity curve here instead of moving or
        // redrawing the GTK surface.
        const base = this._dockActorBaseTranslation ?? {x: 0, y: 0};
        const monitorIndex = this._dockMonitorIndex();
        const positioned = placeDockInWorkArea(
            Main.layoutManager.monitors[monitorIndex],
            this._workAreaForMonitor(monitorIndex),
            placement,
            this._dockAlignment,
            this._dockLocation);
        const offset = calculateDockHideOffset(positioned);
        const collapseRight = rightHideCorridorIntersectsMonitor(
            positioned,
            monitorIndex,
            Main.layoutManager.monitors);

        const serial = ++this._dockVisibilityAnimationSerial;
        this._cancelDockAnimationClip(false);
        actor.remove_all_transitions();
        if (collapseRight) {
            actor.remove_clip();
            actor.translation_x = base.x;
            actor.translation_y = base.y;
            actor.scale_y = 1;
            actor.set_pivot_point(1, 0.5);
            if (!hidden && actor.get_opacity() === 0 &&
                actor.scale_x >= 0.999) {
                actor.scale_x = 0;
            }
        } else {
            actor.scale_x = 1;
            actor.scale_y = 1;
            actor.set_pivot_point(0.5, 0.5);
            if (!hidden && actor.get_opacity() === 0 &&
                actor.translation_x === base.x &&
                actor.translation_y === base.y) {
                actor.translation_x = base.x + offset.x;
                actor.translation_y = base.y + offset.y;
            }
        }
        const startX = actor.translation_x;
        const startY = actor.translation_y;
        const startScaleX = actor.scale_x;
        const targetX = collapseRight
            ? base.x
            : hidden ? base.x + offset.x : base.x;
        const targetY = collapseRight
            ? base.y
            : hidden ? base.y + offset.y : base.y;
        const targetScaleX = collapseRight && hidden ? 0 : 1;
        const fullDistance = Math.max(1, Math.hypot(offset.x, offset.y));
        const remainingAmount = collapseRight
            ? Math.abs(targetScaleX - startScaleX)
            : Math.hypot(targetX - startX, targetY - startY);
        const remainingFraction = Math.min(1, collapseRight
            ? remainingAmount
            : remainingAmount / fullDistance);
        const fullDuration = hidden
            ? DOCK_HIDE_ANIMATION_MS
            : DOCK_REVEAL_ANIMATION_MS;
        const duration = Math.max(60,
            Math.round(fullDuration * remainingFraction));

        this._dockVisibilityState = hidden ? 'hiding' : 'revealing';
        if (!collapseRight)
            this._updateDockAnimationClip(actor, positioned, base);
        this._updateDockRevealActor();
        this._publishDockPointerInside(true);

        let completed = false;
        let completionSource = 0;
        const completeTransition = () => {
            if (completed || serial !== this._dockVisibilityAnimationSerial)
                return;

            completed = true;
            if (completionSource) {
                GLib.source_remove(completionSource);
                completionSource = 0;
            }
            this._cancelDockAnimationClip(false);
            this._dockVisibilityState = hidden ? 'hidden' : 'visible';
            actor.translation_x = targetX;
            actor.translation_y = targetY;
            actor.scale_x = targetScaleX;
            if (!collapseRight)
                this._updateDockAnimationClip(actor, positioned, base);
            actor.set_opacity(hidden ? 0 : this._dockActorOpacity);
            actor.remove_clip();
            this._updateDockRevealActor();
            this._publishDockPointerInside(true);
            this._call(
                'PublishDockAnimationCompleted', '(b)', [hidden]);
        };

        // Clutter does not guarantee an onComplete callback when every
        // animated property already equals its target. Complete that state
        // change here so the C++ controller never waits forever for an
        // animation that was not created.
        if (remainingAmount < (collapseRight ? 0.001 : 0.5)) {
            completeTransition();
            return;
        }

        completionSource = GLib.timeout_add(
            GLib.PRIORITY_DEFAULT,
            duration + 32,
            () => {
                completionSource = 0;
                completeTransition();
                return GLib.SOURCE_REMOVE;
            });
        const animation = {
            duration,
            mode: hidden
                ? Clutter.AnimationMode.EASE_IN_CUBIC
                : Clutter.AnimationMode.EASE_OUT_CUBIC,
            onComplete: completeTransition,
        };
        if (collapseRight)
            animation.scale_x = targetScaleX;
        else {
            animation.translation_x = targetX;
            animation.translation_y = targetY;
        }
        animation.opacity = hidden ? 0 : this._dockActorOpacity;
        actor.ease(animation);
        if (this._isX11DockWindow() && !collapseRight) {
            this._dockClipSource = GLib.timeout_add(
                GLib.PRIORITY_HIGH,
                DOCK_CLIP_FRAME_MS,
                () => {
                    if (serial !== this._dockVisibilityAnimationSerial) {
                        this._dockClipSource = 0;
                        return GLib.SOURCE_REMOVE;
                    }
                    this._updateDockAnimationClip(actor, positioned, base);
                    return GLib.SOURCE_CONTINUE;
                });
        }
    }

    _startDockPlasmaStyleTransition(hidden, actor) {
        const serial = ++this._dockVisibilityAnimationSerial;
        this._cancelDockAnimationClip(false);
        actor.remove_all_transitions();

        // Match Plasma Wayland's map/unmap effect: keep the window fixed at
        // its edge while the complete actor scales into or out of its centre
        // and its compositor opacity fades. Shell owns this transform for
        // GNOME Wayland and native GNOME Shell X11 presentation.
        const base = this._dockActorBaseTranslation ?? {x: 0, y: 0};
        actor.translation_x = base.x;
        actor.translation_y = base.y;
        actor.set_pivot_point(0.5, 0.5);
        actor.remove_clip();

        let startScaleX = actor.scale_x;
        let startScaleY = actor.scale_y;
        if (!hidden && actor.get_opacity() === 0 &&
            startScaleX >= 0.999 && startScaleY >= 0.999) {
            startScaleX = 0;
            startScaleY = 0;
            actor.scale_x = startScaleX;
            actor.scale_y = startScaleY;
        }

        const targetScale = hidden ? 0 : 1;
        const targetOpacity = hidden ? 0 : this._dockActorOpacity;
        const startOpacity = actor.get_opacity();
        const remainingScale = Math.max(
            Math.abs(targetScale - startScaleX),
            Math.abs(targetScale - startScaleY));
        const remainingOpacity = Math.abs(targetOpacity - startOpacity) /
            Math.max(1, this._dockActorOpacity);
        const remainingFraction = Math.min(
            1, Math.max(remainingScale, remainingOpacity));
        const fullDuration = hidden
            ? DOCK_HIDE_ANIMATION_MS
            : DOCK_REVEAL_ANIMATION_MS;
        const duration = Math.max(
            60, Math.round(fullDuration * remainingFraction));

        this._dockVisibilityState = hidden ? 'hiding' : 'revealing';
        this._updateDockRevealActor();
        this._publishDockPointerInside(true);

        let completed = false;
        let completionSource = 0;
        const completeTransition = () => {
            if (completed || serial !== this._dockVisibilityAnimationSerial)
                return;

            completed = true;
            if (completionSource) {
                GLib.source_remove(completionSource);
                completionSource = 0;
            }
            this._dockVisibilityState = hidden ? 'hidden' : 'visible';
            actor.scale_x = targetScale;
            actor.scale_y = targetScale;
            actor.set_opacity(targetOpacity);
            this._updateDockRevealActor();
            this._publishDockPointerInside(true);
            this._call(
                'PublishDockAnimationCompleted', '(b)', [hidden]);
        };

        if (remainingFraction < 0.001) {
            completeTransition();
            return;
        }

        completionSource = GLib.timeout_add(
            GLib.PRIORITY_DEFAULT,
            duration + 32,
            () => {
                completionSource = 0;
                completeTransition();
                return GLib.SOURCE_REMOVE;
            });
        const animation = {
            opacity: targetOpacity,
            duration,
            mode: hidden
                ? Clutter.AnimationMode.EASE_IN_CUBIC
                : Clutter.AnimationMode.EASE_OUT_CUBIC,
            onComplete: completeTransition,
        };
        animation.scale_x = targetScale;
        animation.scale_y = targetScale;
        actor.ease(animation);
    }

    _startDockFadeTransition(hidden, actor) {
        const serial = ++this._dockVisibilityAnimationSerial;
        this._cancelDockAnimationClip(false);
        actor.remove_all_transitions();

        // Shell owns the compositor actor for both native Wayland and
        // XWayland dock surfaces. Keep the committed surface in place and
        // animate only opacity; the existing reveal actor and the C++ input
        // pass-through state remain responsible for the final hidden state.
        const base = this._dockActorBaseTranslation ?? {x: 0, y: 0};
        actor.translation_x = base.x;
        actor.translation_y = base.y;
        actor.scale_x = 1;
        actor.scale_y = 1;
        actor.set_pivot_point(0.5, 0.5);
        actor.remove_clip();

        const targetOpacity = hidden ? 0 : this._dockActorOpacity;
        const startOpacity = actor.get_opacity();
        const fullOpacity = Math.max(1, this._dockActorOpacity);
        const remainingAmount = Math.abs(targetOpacity - startOpacity);
        const remainingFraction = Math.min(
            1, remainingAmount / fullOpacity);
        const fullDuration = hidden
            ? DOCK_HIDE_ANIMATION_MS
            : DOCK_REVEAL_ANIMATION_MS;
        const duration = Math.max(60,
            Math.round(fullDuration * remainingFraction));

        this._dockVisibilityState = hidden ? 'hiding' : 'revealing';
        this._updateDockRevealActor();
        this._publishDockPointerInside(true);

        let completed = false;
        let completionSource = 0;
        const completeTransition = () => {
            if (completed || serial !== this._dockVisibilityAnimationSerial)
                return;

            completed = true;
            if (completionSource) {
                GLib.source_remove(completionSource);
                completionSource = 0;
            }
            this._dockVisibilityState = hidden ? 'hidden' : 'visible';
            actor.set_opacity(targetOpacity);
            this._updateDockRevealActor();
            this._publishDockPointerInside(true);
            this._call(
                'PublishDockAnimationCompleted', '(b)', [hidden]);
        };

        if (remainingAmount < 1) {
            completeTransition();
            return;
        }

        completionSource = GLib.timeout_add(
            GLib.PRIORITY_DEFAULT,
            duration + 32,
            () => {
                completionSource = 0;
                completeTransition();
                return GLib.SOURCE_REMOVE;
            });
        actor.ease({
            opacity: targetOpacity,
            duration,
            mode: Clutter.AnimationMode.EASE_OUT_QUAD,
            onComplete: completeTransition,
        });
    }

    _dockPointerIsInside() {
        if (!this._dockPlacement || this._dockVisibilityState === 'hidden')
            return false;

        const monitorIndex = this._dockMonitorIndex();
        const monitor = Main.layoutManager.monitors[monitorIndex];
        if (!monitor)
            return false;

        const placement = placeDockInWorkArea(
            monitor,
            this._workAreaForMonitor(monitorIndex),
            this._dockPlacement,
            this._dockAlignment,
            this._dockLocation);
        if (!this._pointerPosition)
            return false;

        return isPointerInsideDockInterior(
            placement,
            this._pointerPosition.x,
            this._pointerPosition.y);
    }

    _publishDockPointerInside(force = false) {
        if (!this._connected)
            return;

        const inside = this._dockPointerIsInside();
        if (!force && inside === this._dockPointerInside)
            return;

        this._dockPointerInside = inside;
        this._call('PublishDockPointerInside', '(b)', [inside]);
    }

    _previewPointerIsInside() {
        if (!this._livePreviewOverlay || !this._pointerPosition)
            return false;

        const pointer = this._pointerPosition;

        // The live-preview rectangles cover only the thumbnail bodies. Use
        // the complete placed GTK Preview surface once Shell has classified
        // it so its header, card borders, gaps, and outer padding remain part
        // of the hover region as the pointer travels from the dock.
        for (const window of this._auxiliaryWindowSignals.keys()) {
            try {
                const position = this._auxiliaryPosition(window);
                if (position?.type !== 'preview')
                    continue;

                const frame = window.get_frame_rect();
                if (frame.width <= 0 || frame.height <= 0)
                    continue;

                const placed = clampAuxiliaryToWorkArea(
                    position,
                    frame,
                    this._workAreaForMonitor(this._dockMonitorIndex()));
                const previewRect = {
                    x: placed.x,
                    y: placed.y,
                    width: frame.width,
                    height: frame.height,
                };
                if (isPointInsideRect(previewRect, pointer.x, pointer.y))
                    return true;
            } catch (_error) {
                // The short-lived GTK surface can disappear between pointer
                // samples; the live thumbnail bounds below remain safe.
            }
        }

        // Classification can trail ShowLivePreviews by one compositor frame.
        // Preserve the thumbnail-body fallback during that short interval.
        return this._livePreviewRects.some(rect =>
            isPointInsideRect(rect, pointer.x, pointer.y));
    }

    _updateLivePreviewSelectors(force = false) {
        for (const rect of this._livePreviewRects) {
            const selected = Boolean(this._pointerPosition &&
                isPointInsideRect(
                    rect,
                    this._pointerPosition.x,
                    this._pointerPosition.y));
            if (!force && selected === rect.selected)
                continue;

            rect.selected = selected;
            rect.selector.set_style(selected
                ? `background-color: ${this._previewSelectorFill}; ` +
                    'border-radius: 6px;'
                : 'background-color: transparent; ' +
                    'border-radius: 6px;');
            rect.selectionOutline.set_style(selected
                ? `border: 2px solid ${this._previewSelectorOutline}; ` +
                    'border-radius: 6px;'
                : 'border: 2px solid transparent; ' +
                    'border-radius: 6px;');
        }
    }

    _publishPreviewPointerInside(force = false) {
        this._updateLivePreviewSelectors();

        if (!this._connected)
            return;

        const inside = this._previewPointerIsInside();
        if (!force && inside === this._previewPointerInside)
            return;

        this._previewPointerInside = inside;
        this._call('PublishPreviewPointerInside', '(b)', [inside]);
    }

    _scheduleDockPlacement(restart = false, requestedDelay = null) {
        if (!this._enabled || !this._dockWindow)
            return;

        if (this._dockPlacementSource) {
            if (!restart)
                return;
            GLib.source_remove(this._dockPlacementSource);
            this._dockPlacementSource = 0;
        }

        const delay = requestedDelay ?? (this._dockTransitioning
            ? DOCK_TRANSITION_DELAY_MS
            : DOCK_PLACEMENT_DELAY_MS);

        this._dockPlacementSource = GLib.timeout_add(
            GLib.PRIORITY_DEFAULT,
            delay,
            () => {
                this._dockPlacementSource = 0;
                const committed = this._placeDockWindow();
                if (committed) {
                    this._finishDockTransition();
                } else if (++this._dockPlacementAttempts <
                    DOCK_PLACEMENT_MAX_ATTEMPTS) {
                    // move_frame() is asynchronous on Wayland. Keep the
                    // painted actor at the requested edge and retry until
                    // Mutter's frame rectangle (plus any strut compensation)
                    // proves that the dock has committed. Only an explicitly
                    // guarded transition suppresses actor opacity meanwhile.
                    this._scheduleDockPlacement(
                        false, DOCK_PLACEMENT_DELAY_MS);
                } else {
                    // Do not retry forever or leave DockLight permanently
                    // invisible if a compositor rejects movement or a window
                    // disappears.
                    this._finishDockTransition();
                }
                return GLib.SOURCE_REMOVE;
            });
    }

    _setDockPlacement(values) {
        const [available, x, y, width, height] = values || [];
        if (!available || width <= 0 || height <= 0) {
            this._dockPlacement = null;
            this._removeDockStrut();
            this._updateDockRevealActor();
            return;
        }

        const placement = {
            x: Math.round(x),
            y: Math.round(y),
            width: Math.round(width),
            height: Math.round(height),
        };
        const current = this._dockPlacement;
        if (current && current.x === placement.x && current.y === placement.y &&
            current.width === placement.width && current.height === placement.height)
            return;

        const monitorChanged = dockPlacementChangesMonitor(
            current,
            placement,
            Main.layoutManager.monitors,
            Main.layoutManager.primaryIndex);
        this._dockPlacementAttempts = 0;
        this._dockPlacement = placement;
        this._enforceDockWindowLayer();
        this._updateDockRevealActor();
        // Routine DockItem, home-item, or icon-size changes resize the mapped
        // dock without creating a provisional surface. Keep its actor visible
        // and preserve any current autohide transform. Moving the placement to
        // another monitor still needs the guarded compositor transition.
        if (monitorChanged)
            this._beginDockTransition();
        this._scheduleDockPlacement(true);
    }

    _ensureDockRevealActor() {
        if (this._dockRevealActor)
            return;

        this._dockRevealActor = new St.Widget({
            reactive: true,
            track_hover: true,
            can_focus: false,
            visible: false,
        });
        this._dockRevealSignal = this._dockRevealActor.connect(
            'notify::hover', actor => {
                if (!actor.hover || !this._enabled ||
                    this._dockAutohide === 'none' ||
                    this._dockVisibilityState !== 'hidden')
                    return;

                // The GTK surface stays mapped at its committed edge. Reveal
                // only restores its content and input region, so Mutter never
                // gets a new provisional centred surface to place.
                this._call('RequestDockReveal', null, null, (reply, error) => {
                    if (!error && reply?.[0] === true)
                        this._dockHidden = false;
                    this._updateDockRevealActor();
                });
            });
        Main.layoutManager.addChrome(this._dockRevealActor, {
            affectsStruts: false,
            affectsInputRegion: true,
            trackFullscreen: true,
        });
    }

    _updateDockRevealActor() {
        const actor = this._dockRevealActor;
        if (!actor)
            return;

        if (!this._enabled || this._dockAutohide === 'none' ||
            this._dockVisibilityState !== 'hidden' || !this._dockPlacement) {
            actor.hide();
            return;
        }

        const monitorIndex = this._dockMonitorIndex();
        const monitor = Main.layoutManager.monitors[monitorIndex];
        if (!monitor) {
            actor.hide();
            return;
        }

        const placement = placeDockInWorkArea(
            monitor,
            this._workAreaForMonitor(monitorIndex),
            this._dockPlacement,
            this._dockAlignment,
            this._dockLocation);
        const reveal = calculateDockRevealRect(placement);

        actor.set_position(reveal.x, reveal.y);
        actor.set_size(reveal.width, reveal.height);
        actor.show();
    }

    _destroyDockRevealActor() {
        const actor = this._dockRevealActor;
        this._dockRevealActor = null;
        if (!actor)
            return;

        if (this._dockRevealSignal) {
            try {
                actor.disconnect(this._dockRevealSignal);
            } catch (_error) {
                // The actor may already be disposed during Shell teardown.
            }
        }
        this._dockRevealSignal = 0;
        try {
            Main.layoutManager.removeChrome(actor);
        } catch (_error) {
            // Shell may already have removed its chrome during teardown.
        }
        try {
            actor.destroy();
        } catch (_error) {
            // removeChrome() may dispose the actor on this Shell release.
        }
    }

    _requestDockPlacement() {
        this._call('GetDockPlacementGeometry', null, null, (reply) => {
            if (reply)
                this._setDockPlacement(reply);
        });
    }

    _dockMonitorIndex() {
        return dockMonitorIndexForRect(
            this._dockPlacement,
            Main.layoutManager.monitors,
            Main.layoutManager.primaryIndex);
    }

    _refreshNativeWorkAreas() {
        this._nativeWorkAreas = Main.layoutManager.monitors.map(
            (_monitor, index) => {
                const area = Main.layoutManager.getWorkAreaForMonitor(index);
                return area
                    ? {x: area.x, y: area.y, width: area.width, height: area.height}
                    : null;
            });
    }

    _workAreaForMonitor(index) {
        return this._nativeWorkAreas[index] ||
            Main.layoutManager.getWorkAreaForMonitor(index) ||
            Main.layoutManager.monitors[index] || null;
    }

    _removeDockStrut() {
        if (!this._dockStrut)
            return;

        // Clear the shared reference before either Shell operation. Depending
        // on the Shell version and shutdown timing, removeChrome() can dispose
        // the actor; a subsequent destroy() then throws and previously left a
        // disposed object in _dockStrut for the next placement pass.
        const dockStrut = this._dockStrut;
        this._dockStrut = null;
        try {
            Main.layoutManager.removeChrome(dockStrut);
        } catch (_error) {
            // Shell may already have removed its chrome during teardown.
        }
        try {
            dockStrut.destroy();
        } catch (_error) {
            // removeChrome() may have disposed it on this Shell release.
        }
    }

    _updateDockStrut(monitor, dockRect) {
        if (this._dockAutohide !== 'none') {
            this._removeDockStrut();
            return {x: 0, y: 0};
        }

        if (!this._dockStrut) {
            this._dockStrut = new St.Widget({
                reactive: false,
                can_focus: false,
                opacity: 0,
            });
            Main.layoutManager.addChrome(this._dockStrut, {
                affectsStruts: true,
                affectsInputRegion: false,
                trackFullscreen: false,
            });
        }

        const strut = calculateDockStrut(monitor, dockRect);

        this._dockStrut.set_position(
            Math.round(strut.x), Math.round(strut.y));
        this._dockStrut.set_size(
            Math.round(strut.width), Math.round(strut.height));

        return strut.actorOffset;
    }

    _placeDockWindow() {
        const window = this._dockWindow;
        const placement = this._dockPlacement;
        if (!window || !placement)
            return false;

        // XWayland docks already own their position and reservation through
        // GTK/X11 and _NET_WM_STRUT_PARTIAL. Adding a Shell chrome strut and
        // moving the Meta.Window as well makes Docklight consume its own work
        // area on every monitor notification. Keep the GNOME integration for
        // window discovery and compositor autohide animation, but publish the
        // real X11 frame without applying a second placement policy.
        if (this._isX11DockWindow(window)) {
            this._removeDockStrut();
            const rect = window.get_frame_rect();
            this._dockActorBaseTranslation = {x: 0, y: 0};
            const actor = window.get_compositor_private?.();
            if (actor && !['revealing', 'hiding'].includes(
                this._dockVisibilityState)) {
                actor.translation_x = 0;
                actor.translation_y = 0;
            }
            this._publishDockSurfaceGeometry(rect);
            return true;
        }

        const monitorIndex = this._dockMonitorIndex();
        const monitor = Main.layoutManager.monitors[monitorIndex];
        if (!monitor)
            return false;
        const rect = window.get_frame_rect();
        const positionedPlacement = placeDockInWorkArea(
            monitor,
            this._workAreaForMonitor(monitorIndex),
            placement,
            this._dockAlignment,
            this._dockLocation);
        const x = positionedPlacement.x;
        const y = positionedPlacement.y;
        if (rect.x !== x || rect.y !== y)
            // This is Shell-managed dock placement, not an interactive user
            // move. A user operation makes Mutter apply its normal toplevel
            // edge constraints, adding an 8 px inset on some screen edges;
            // that inset is then exposed as a gap beside the dock strut.
            window.move_frame(false, x, y);

        const actorOffset = this._updateDockStrut(monitor, positionedPlacement);

        const committed = isDockPlacementCommitted(
            rect, positionedPlacement, actorOffset);

        // move_frame() is asynchronous on Wayland. Until Mutter commits it,
        // keep the compositor actor painted at the requested edge instead of
        // at Mutter's provisional centred frame. Once the frame arrives the
        // correction becomes zero, leaving only the deliberate strut offset.
        // This also makes the reveal robust if another Shell component
        // restores actor opacity before our guarded transition completes.
        const actor = window.get_compositor_private?.();
        if (actor) {
            if (committed) {
                this._dockActorBaseTranslation = {...actorOffset};
                if (!['revealing', 'hiding'].includes(
                    this._dockVisibilityState)) {
                    actor.translation_x = actorOffset.x;
                    actor.translation_y = actorOffset.y;
                }
            } else {
                actor.translation_x = x - rect.x;
                actor.translation_y = y - rect.y;
            }
        }

        // Publish the C++ layout result after resolving it against Shell's
        // native work area; never derive geometry from the displaced frame.
        if (committed)
            this._publishDockSurfaceGeometry(positionedPlacement);
        return committed;
    }

    _publishDockSurfaceGeometry(rect) {
        const geometry = rect || {x: 0, y: 0, width: 0, height: 0};
        this._publish('PublishDockSurfaceGeometry', '(siiii)', [
            this._nextRevision(),
            Math.round(geometry.x),
            Math.round(geometry.y),
            Math.round(geometry.width),
            Math.round(geometry.height),
        ]);
    }

    _connect(object, signal, callback) {
        const id = object.connect(signal, callback);
        this._signals.push([object, id]);
    }

    _call(method, signature, values, callback = () => { }) {
        if (!this._proxy)
            return;

        const parameters = signature ? new GLib.Variant(signature, values) : null;
        this._proxy.call(
            method,
            parameters,
            Gio.DBusCallFlags.NONE,
            -1,
            null,
            (proxy, result) => {
                try {
                    callback(proxy.call_finish(result).deepUnpack());
                } catch (error) {
                    callback(null, error);
                }
            });
    }

    _register() {
        if (!this._proxy || !this._serviceAvailable ||
            this._connected || this._registering)
            return;

        this._registering = true;
        this._call('Register', '(s)', [PROTOCOL_VERSION], (reply) => {
            this._registering = false;
            this._connected = Boolean(reply?.[0]);
            if (!this._connected) {
                // At login the application may own its bus name just before
                // its object is exported. The name watcher fires only once,
                // so retry that short startup race explicitly.
                this._scheduleRegistrationRetry();
                return;
            }

            if (this._registrationRetrySource) {
                GLib.source_remove(this._registrationRetrySource);
                this._registrationRetrySource = 0;
            }

            this._revision = 0;
            this._pendingWaits = 0;
            if (this._waylandIntegration) {
                this._call('GetIconGeometries', null, null, reply => {
                    if (!this._connected)
                        return;
                    for (const geometry of reply?.[0] || [])
                        this._setIconGeometry(...geometry);
                });
            } else {
                // The bridge backend accepts incremental dock geometry only
                // after its initial snapshot commits. Establish that protocol
                // connection with an intentionally empty snapshot: native
                // EWMH/XComposite remains the sole owner of X11 app windows.
                this._publishAnimationOnlySnapshot();
            }
            this._requestDockPlacement();
            this._call('GetDockHidden', null, null, reply => {
                this._dockHidden = Boolean(reply?.[0]);
                this._dockVisibilityState = this._dockHidden
                    ? 'hidden'
                    : 'visible';
                this._updateDockRevealActor();
                this._publishDockPointerInside(true);
            });

            // The initial actor scan can run before Mutter has populated the
            // dock's application identity and final window metadata. Repeat
            // it after registration so an already-mapped dock is reliably
            // discovered and placed.
            for (const actor of global.get_window_actors())
                this._considerDockWindow(actor.meta_window);
            this._scheduleDockDiscoveryScan();

            if (this._waylandIntegration) {
                this._publishSnapshot();
                this._publishCurrentDesktop();
                this._waitForCommands();
            }
        });
    }

    _scheduleRegistrationRetry() {
        if (!this._enabled || !this._proxy || !this._serviceAvailable ||
            this._connected || this._registrationRetrySource)
            return;

        this._registrationRetrySource = GLib.timeout_add(
            GLib.PRIORITY_DEFAULT,
            REGISTRATION_RETRY_MS,
            () => {
                this._registrationRetrySource = 0;
                this._register();
                return GLib.SOURCE_REMOVE;
            });
    }

    _disconnectBackend() {
        if (!this._waylandIntegration)
            this._restoreX11DockActor();

        this._connected = false;
        this._registering = false;
        this._pendingWaits = 0;
        this._clearIconGeometries();
        this._destroyLivePreviews();

        // GNOME Shell and this extension outlive an app-only Docklight
        // restart. The replacement Wayland surface initially has only its
        // shared application id, so allow the early dock fallback again for
        // the new service lifetime. _isDockWindow() still requires that the
        // previous dock has been unmanaged before this fallback can win.
        this._dockDiscoveredOnce = false;

        if (this._registrationRetrySource) {
            GLib.source_remove(this._registrationRetrySource);
            this._registrationRetrySource = 0;
        }
    }

    _nextRevision() {
        this._revision++;
        return String(this._revision);
    }

    _publish(method, signature, values) {
        if (!this._connected) {
            this._register();
            return;
        }

        this._call(method, signature, values, (reply) => {
            if (reply?.[0] !== true) {
                this._disconnectBackend();
                this._register();
            }
        });
    }

    _windowId(window) {
        return window ? String(window.get_stable_sequence()) : '';
    }

    _setIconGeometry(windowId, x, y, width, height) {
        if (!this._connected || !this._iconGeometries || !this._windows ||
            !windowId || width <= 0 || height <= 0)
            return;

        const geometry = {x, y, width, height};
        this._iconGeometries.set(windowId, geometry);

        const window = this._windows.get(windowId);
        if (!window)
            return;

        // get_frame_rect() supplies the correct boxed rectangle type on both
        // Meta.Rectangle (GNOME 45) and Mtk.Rectangle (GNOME 46+).
        try {
            const rect = window.get_frame_rect();
            Object.assign(rect, geometry);
            window.set_icon_geometry(rect);
        } catch (error) {
            logError(error,
                `Failed to set Docklight icon geometry for window ${windowId}`);
        }
    }

    _removeIconGeometry(windowId) {
        if (!windowId)
            return;

        this._iconGeometries?.delete(windowId);
        try {
            this._windows?.get(windowId)?.set_icon_geometry(null);
        } catch (_error) {
            // The Meta.Window may already be finalizing.
        }
    }

    _clearIconGeometries() {
        if (!this._iconGeometries || !this._windows)
            return;

        for (const windowId of this._iconGeometries.keys()) {
            try {
                this._windows.get(windowId)?.set_icon_geometry(null);
            } catch (_error) {
                // The Meta.Window may already be finalizing.
            }
        }
        this._iconGeometries.clear();
    }

    _isThumbnailCallerAuthorized(invocation) {
        // Window textures are compositor-private data. Only the Docklight
        // process that owns the integration service may request captures or
        // place clones; stable sequence ids alone must not become a window
        // screenshot capability for every process on the session bus.
        const owner = this._proxy?.get_name_owner?.();
        return this._enabled &&
            this._connected &&
            Boolean(owner) &&
            invocation?.get_sender?.() === owner;
    }

    async CaptureWindowAsync(params, invocation) {
        const [windowId, targetWidth, targetHeight] = params;
        const emptyReply = () => invocation.return_value(
            new GLib.Variant('(ay)', [new Uint8Array()]));

        if (!this._isThumbnailCallerAuthorized(invocation) ||
            targetWidth <= 0 || targetHeight <= 0) {
            emptyReply();
            return;
        }

        const window = this._windows.get(windowId);
        const actor = window?.get_compositor_private?.();
        if (!actor || actor.is_destroyed?.()) {
            emptyReply();
            return;
        }

        try {
            // Mutter paints the window actor offscreen, so the result is not
            // affected by overlapping windows and works for both native
            // Wayland and XWayland clients. Scale in Shell before the GPU
            // readback and PNG encoding: transferring a full browser-sized
            // frame only for GTK to shrink it made live video previews
            // needlessly expensive and visibly stuttered.
            const content = actor.paint_to_content(null);
            const texture = content?.get_texture?.();
            if (!texture) {
                emptyReply();
                return;
            }

            const sourceWidth = texture.get_width?.() || 0;
            const sourceHeight = texture.get_height?.() || 0;
            if (sourceWidth <= 0 || sourceHeight <= 0) {
                emptyReply();
                return;
            }

            const scale = Math.min(
                1,
                targetWidth / sourceWidth,
                targetHeight / sourceHeight);

            const stream = Gio.MemoryOutputStream.new_resizable();
            await Shell.Screenshot.composite_to_stream(
                texture,
                0, 0, -1, -1,
                scale,
                null, 0, 0, 1,
                stream);
            stream.close(null);
            const png = stream.steal_as_bytes().get_data();
            invocation.return_value(new GLib.Variant('(ay)', [png]));
        } catch (error) {
            logError(error, `Failed to capture Docklight thumbnail for ${windowId}`);
            emptyReply();
        }
    }

    HoldLivePreviewSurfaceAsync(_params, invocation) {
        if (!this._isThumbnailCallerAuthorized(invocation) ||
            !Meta.is_wayland_compositor()) {
            invocation.return_value(null);
            return;
        }

        try {
            for (const window of this._auxiliaryWindowSignals.keys()) {
                const position = this._auxiliaryPosition(window);
                if (position?.type !== 'preview')
                    continue;

                const actor = window.get_compositor_private?.();
                if (!actor || actor.is_destroyed?.())
                    continue;

                // GTK deliberately remaps replacements at near-zero opacity.
                // Never replace a good held frame with that transparent
                // staging actor during a fast multi-item crossing.
                if ((actor.get_paint_opacity?.() ?? 255) < 128)
                    continue;

                const rect = window.get_frame_rect();
                if (rect.width <= 0 || rect.height <= 0)
                    continue;

                // Capture the complete GTK surface before XWayland unmaps it.
                // The retained live-clone overlay is raised above this shield,
                // preserving both the background/header and moving video.
                const content = actor.paint_to_content(null);
                if (!content?.get_texture?.())
                    continue;

                const target = clampAuxiliaryToWorkArea(
                    position,
                    rect,
                    this._workAreaForMonitor(this._dockMonitorIndex()));
                const shield = new Clutter.Actor({
                    reactive: false,
                    content,
                    content_gravity: Clutter.ContentGravity.RESIZE_FILL,
                    x: target.x,
                    y: target.y,
                    width: rect.width,
                    height: rect.height,
                });
                // A rapid pointer crossing can request another replacement
                // while GTK is already unmapped. Replace the prior shield
                // only after a new complete surface was captured; otherwise
                // keep the prior frame covering the whole remap chain.
                this._destroyPreviewReplacementShield();
                Main.uiGroup.add_child(shield);
                this._previewReplacementShield = shield;

                if (this._livePreviewOverlay) {
                    Main.uiGroup.set_child_above_sibling(
                        this._livePreviewOverlay, shield);
                }
                break;
            }
        } catch (error) {
            logError(error, 'Failed to hold Docklight preview surface');
        }

        invocation.return_value(null);
    }

    ShowLivePreviewsAsync(params, invocation) {
        const [previews] = params;
        if (!this._isThumbnailCallerAuthorized(invocation)) {
            invocation.return_value(null);
            return;
        }

        const openingSession = !this._previewSessionOpen;
        this._previewSessionOpen = true;

        // Do not publish a transient outside state while replacing one set of
        // previews with another. The new rectangles are installed below and
        // immediately reconciled against Shell's compositor pointer.
        this._destroyLivePreviews({
            publishPointerOutside: false,
            preserveSession: true,
        });

        // WindowPreviewLayout actors must remain in Shell's UI layer. Making
        // them children of a real MetaWindowActor creates an unsupported
        // paint dependency and can stop live video damage from reaching the
        // clones. The full-stage container and layout-created descendants
        // remain non-reactive. The GTK card underneath remains the sole input
        // owner and forwards PiP clicks through the integration service.
        const overlay = new Clutter.Actor({
            reactive: false,
            x: 0,
            y: 0,
            width: global.stage.width,
            height: global.stage.height,
        });

        const previewRects = [];
        // Animate a closed-to-open transition only. Adjacent DockItems reuse
        // the live-overlay lifetime and replace their actors without passing
        // through a low-opacity frame.
        const waylandPreviewOpening =
            Meta.is_wayland_compositor() && openingSession;
        const animatePreviews =
            !Meta.is_wayland_compositor() || waylandPreviewOpening;
        const previewFadeActors = [];
        for (const [windowId, x, y, width, height] of previews) {
            if (width <= 0 || height <= 0)
                continue;

            const window = this._windows.get(windowId);
            const windowActor = window?.get_compositor_private?.();
            if (!window || !windowActor || windowActor.is_destroyed?.())
                continue;
            const contentActor = windowActor.get_last_child?.();

            const frame = window.get_frame_rect();
            if (frame.width <= 0 || frame.height <= 0)
                continue;

            const scale = Math.min(
                width / frame.width,
                height / frame.height);
            const previewWidth = Math.max(1, Math.round(frame.width * scale));
            const previewHeight = Math.max(1, Math.round(frame.height * scale));
            const previewOffsetX = Math.floor((width - previewWidth) / 2);
            const previewOffsetY = Math.floor((height - previewHeight) / 2);
            const layout = new Shell.WindowPreviewLayout();
            const preview = new Clutter.Actor({
                reactive: false,
                clip_to_allocation: true,
                opacity: animatePreviews && !waylandPreviewOpening ? 0 : 255,
                x: previewOffsetX,
                y: previewOffsetY,
                width: previewWidth,
                height: previewHeight,
            });

            // WindowPreviewLayout tracks its container during assignment, so
            // match GNOME Shell's own construction order instead of passing
            // it as a construct property.
            preview.layout_manager = layout;
            const clone = layout.add_window(window);
            if (!clone)
                continue;
            // Keep the layout's geometry and lifetime tracking, but exclude
            // compositor effects attached to the WindowActor from its clone.
            // The final child is the X11 surface actor or Wayland container.
            if (contentActor && contentActor !== windowActor &&
                contentActor.is_destroyed?.() !== true)
                clone.source = contentActor;
            // Keep the compositor clone paint-only so repeatedly replacing
            // previews cannot leave stale Shell chrome in the stage input
            // region. The GTK card underneath remains responsible for mouse
            // input, while Shell's pointer poll drives this visual selector.
            // Keep the area below the clone transparent: GTK maintains a
            // cached snapshot there for the short periods in which Mutter has
            // not produced a usable clone frame yet.
            const selector = new St.Widget({
                reactive: false,
                x,
                y,
                width,
                height,
                style: 'background-color: transparent; ' +
                    'border-radius: 6px;',
            });
            if (waylandPreviewOpening) {
                selector.set_pivot_point(0.5, 0.5);
                selector.scale_x = PREVIEW_VISIBILITY_MIN_SCALE;
                selector.scale_y = PREVIEW_VISIBILITY_MIN_SCALE;
                selector.opacity = PREVIEW_VISIBILITY_INITIAL_OPACITY;
            }
            // Keep a border above the live clone. The selector fill must stay
            // below the clone so alpha in a PiP surface cannot tint the video,
            // but a fill alone is then invisible over the image and makes the
            // old GTK hover look stuck. This non-reactive outline provides an
            // authoritative, visibly moving selection without taking input.
            const selectionOutline = new St.Widget({
                reactive: false,
                x: 0,
                y: 0,
                width,
                height,
                style: 'border: 2px solid transparent; ' +
                    'border-radius: 6px;',
            });
            selector.add_child(preview);
            selector.add_child(selectionOutline);
            selector.set_child_above_sibling(selectionOutline, preview);
            overlay.add_child(selector);
            // WindowPreviewLayout owns the compositor clone actors it creates.
            // Keep those descendants non-reactive as well: preview layouts
            // may add clone actors after constructing the container.
            const disableDescendantInput = actor => {
                for (const child of actor.get_children()) {
                    child.reactive = false;
                    disableDescendantInput(child);
                }
            };
            disableDescendantInput(preview);
            previewRects.push({
                windowId,
                x,
                y,
                width,
                height,
                selector,
                selectionOutline,
                selected: false,
            });
            if (animatePreviews)
                previewFadeActors.push(
                    waylandPreviewOpening ? selector : preview);
        }

        if (previewRects.length > 0) {
            Main.uiGroup.add_child(overlay);
            this._livePreviewOverlay = overlay;
            this._livePreviewRects = previewRects;
            this._publishPreviewPointerInside(true);
            // Keep the full-stage overlay untransformed; only bounded cards
            // participate in the opening effect.
            for (const previewActor of previewFadeActors) {
                if (waylandPreviewOpening) {
                    previewActor.ease({
                        scale_x: 1,
                        scale_y: 1,
                        opacity: 255,
                        duration: PREVIEW_VISIBILITY_ANIMATION_MS,
                        mode: Clutter.AnimationMode.EASE_IN_OUT_QUINT,
                    });
                } else {
                    previewActor.ease({
                        opacity: 255,
                        duration: PREVIEW_CLONE_FADE_MS,
                        mode: Clutter.AnimationMode.EASE_OUT_QUAD,
                    });
                }
            }
            if (!openingSession)
                this._releasePreviewReplacementShield();
        } else {
            overlay.destroy();
            this._destroyLivePreviews();
        }

        invocation.return_value(null);
    }

    SetPreviewColorAsync(params, invocation) {
        if (this._isThumbnailCallerAuthorized(invocation)) {
            const fallback = [105 / 255, 170 / 255, 1, 1];
            const channels = params.map((value, index) =>
                Number.isFinite(value)
                    ? Math.min(1, Math.max(0, value))
                    : fallback[index]);
            const rgb = channels.slice(0, 3).map(value =>
                Math.round(value * 255));
            this._previewSelectorFill =
                `rgba(${rgb.join(', ')}, ${0.32 * channels[3]})`;
            this._previewSelectorOutline =
                `rgba(${rgb.join(', ')}, ${0.95 * channels[3]})`;
            this._updateLivePreviewSelectors(true);
        }
        invocation.return_value(null);
    }

    ForwardPreviewPrimaryClickAsync(params, invocation) {
        const [windowId, normalizedX, normalizedY] = params;
        if (this._isThumbnailCallerAuthorized(invocation)) {
            const window = this._windows.get(windowId);
            if (this._isApplicationAuxiliary(window))
                this._forwardPreviewPrimaryClickAt(
                    window, normalizedX, normalizedY);
        }
        invocation.return_value(null);
    }

    _forwardPreviewPrimaryClickAt(window, normalizedX, normalizedY) {
        const frame = window?.get_frame_rect?.();
        if (!frame || frame.width <= 0 || frame.height <= 0 ||
            !Number.isFinite(normalizedX) || !Number.isFinite(normalizedY))
            return;

        // WindowPreviewLayout preserves the source aspect ratio. Translate
        // the point on its compositor clone back into the real client frame.
        // The GTK card uses this same normalized path during the first frame,
        // before Shell has committed the new thumbnail input region. Thus an
        // early PiP click cannot fall through to GTK's show/raise action.
        const sourceX = frame.x + Math.min(frame.width - 1, Math.max(0,
            normalizedX * frame.width));
        const sourceY = frame.y + Math.min(frame.height - 1, Math.max(0,
            normalizedY * frame.height));

        const clickTime = GLib.get_monotonic_time();
        const previousClick = this._previewPointerLastClick;
        if (previousClick && previousClick.window === window &&
            clickTime - previousClick.time < PREVIEW_DOUBLE_CLICK_GUARD_US &&
            Math.abs(sourceX - previousClick.x) <=
                PREVIEW_DOUBLE_CLICK_DISTANCE_PX &&
            Math.abs(sourceY - previousClick.y) <=
                PREVIEW_DOUBLE_CLICK_DISTANCE_PX) {
            return;
        }
        this._previewPointerLastClick = {
            window,
            x: sourceX,
            y: sourceY,
            time: clickTime,
        };

        // PiP controls in browsers are mouse controls rather than touch
        // targets. A virtual touchscreen also makes Shell draw its touch
        // feedback circle. Defer the virtual pointer sequence until after the
        // physical release handler returns: while that handler is running,
        // Mutter's implicit grab still routes injected events back to Shell.
        // Separate motion, press, release, and restoration as well so the
        // Wayland client observes focus/motion before its button event.
        const restoreX = this._pointerPosition?.x ?? sourceX;
        const restoreY = this._pointerPosition?.y ?? sourceY;
        this._cancelPreviewPointerInput(false, false);
        // Tell GTK to preserve its current hover, preview, and autohide state
        // before moving the compositor's core pointer. The movement generates
        // ordinary GDK crossing events even though it exists only to forward
        // one PiP click.
        this._setPreviewInputForwarding(true, accepted => {
            if (!this._enabled || !accepted)
                return;

            // The compositor clone and its GTK backing window can overlap the
            // real PiP surface (especially for a bottom dock and Firefox's
            // bottom-right PiP placement). Temporarily remove those surfaces
            // from picking; otherwise the virtual click returns to Docklight.
            this._suppressPreviewInput();
            this._schedulePreviewPointerStep(16, () => {
                if (!this._previewPointerDevice) {
                    const seat = Clutter.get_default_backend().get_default_seat();
                    this._previewPointerDevice = seat.create_virtual_device(
                        Clutter.InputDeviceType.POINTER_DEVICE);
                }
                this._previewPointerRestorePosition = {
                    x: restoreX,
                    y: restoreY,
                    deadline: GLib.get_monotonic_time() + 250000,
                    restoring: false,
                };
                this._previewPointerDevice.notify_absolute_motion(
                    GLib.get_monotonic_time(), sourceX, sourceY);
                this._schedulePreviewPointerStep(16, () => {
                    this._previewPointerDevice.notify_button(
                        GLib.get_monotonic_time(),
                        Clutter.BUTTON_PRIMARY,
                        Clutter.ButtonState.PRESSED);
                    this._previewPointerPressed = true;
                    this._schedulePreviewPointerStep(24, () => {
                        this._previewPointerDevice.notify_button(
                            GLib.get_monotonic_time(),
                            Clutter.BUTTON_PRIMARY,
                            Clutter.ButtonState.RELEASED);
                        this._previewPointerPressed = false;
                        // Firefox may apply its PiP double-click maximize after
                        // the release has returned to Shell. Undo only a
                        // maximize caused during this forwarded preview click;
                        // otherwise the resized source invalidates the remaining
                        // coordinate/restore sequence and destabilizes input.
                        this._schedulePreviewPointerStep(100, () => {
                            const maximized = window.get_maximized?.();
                            if (maximized !== undefined &&
                                maximized !== Meta.MaximizeFlags.NONE) {
                                window.unmaximize(Meta.MaximizeFlags.BOTH);
                            }
                        });
                        this._schedulePreviewPointerStep(16, () => {
                            if (this._previewPointerRestorePosition)
                                this._previewPointerRestorePosition.restoring = true;
                            this._previewPointerDevice.notify_absolute_motion(
                                GLib.get_monotonic_time(), restoreX, restoreY);
                            this._schedulePreviewPointerStep(16, () => {
                                this._restorePreviewInput();
                                this._setPreviewInputForwarding(false);
                            });
                        });
                    });
                });
            });
        });
    }

    _setPreviewInputForwarding(forwarding, callback = () => {}) {
        if (!this._proxy) {
            callback(false);
            return;
        }

        this._call(
            'PublishPreviewInputForwarding',
            '(b)',
            [forwarding],
            (reply, error) => callback(Boolean(reply?.[0]), error));
    }

    _schedulePreviewPointerStep(delay, callback) {
        const source = GLib.timeout_add(
            GLib.PRIORITY_DEFAULT,
            delay,
            () => {
                this._previewPointerSources.delete(source);
                if (!this._enabled)
                    return GLib.SOURCE_REMOVE;
                try {
                    callback();
                } catch (error) {
                    logError(error, 'Failed to forward Docklight PiP input');
                    this._cancelPreviewPointerInput(true);
                }
                return GLib.SOURCE_REMOVE;
            });
        this._previewPointerSources.add(source);
    }

    _suppressPreviewInput() {
        this._restorePreviewInput();

        const actors = [];
        const appendActor = actor => {
            if (actor && !actor.is_destroyed?.() && !actors.includes(actor))
                actors.push(actor);
        };

        appendActor(this._livePreviewOverlay);
        appendActor(this._dockWindow?.get_compositor_private?.());

        for (const window of this._auxiliaryWindowSignals.keys()) {
            if (this._auxiliaryPosition(window)?.type !== 'preview')
                continue;
            appendActor(window.get_compositor_private?.());
        }

        this._previewInputSuppressedActors = actors.map(actor => ({
            actor,
            reactive: actor.get_reactive?.() ?? actor.reactive,
        }));
        for (const {actor} of this._previewInputSuppressedActors)
            actor.set_reactive(false);
    }

    _restorePreviewInput() {
        for (const {actor, reactive} of this._previewInputSuppressedActors) {
            try {
                if (!actor.is_destroyed?.())
                    actor.set_reactive(reactive);
            } catch (_error) {
                // A preview can be closed while input is being forwarded.
            }
        }
        this._previewInputSuppressedActors = [];
    }

    _cancelPreviewPointerInput(
        disposeDevice = false,
        finishForwarding = true) {
        for (const source of this._previewPointerSources)
            GLib.source_remove(source);
        this._previewPointerSources.clear();

        const device = this._previewPointerDevice;
        if (device && this._previewPointerPressed) {
            try {
                device.notify_button(
                    GLib.get_monotonic_time(),
                    Clutter.BUTTON_PRIMARY,
                    Clutter.ButtonState.RELEASED);
            } catch (_error) {
                // Disposing the device below also clears its button state.
            }
        }
        this._previewPointerPressed = false;
        this._restorePreviewInput();
        if (finishForwarding)
            this._setPreviewInputForwarding(false);

        const restore = this._previewPointerRestorePosition;
        if (device && restore) {
            restore.restoring = true;
            restore.deadline = GLib.get_monotonic_time() + 250000;
            try {
                device.notify_absolute_motion(
                    GLib.get_monotonic_time(), restore.x, restore.y);
            } catch (_error) {
                // The logical-pointer deadline still releases hover state.
            }
        }
        // Clutter queues virtual input for delivery after this main-loop
        // callback. Disposing the device after the apparent final motion can
        // discard the queued button sequence before the Wayland client sees
        // it. Reuse the device between preview clicks and dispose it only on
        // teardown or after an injection error.
        if (disposeDevice) {
            this._previewPointerDevice = null;
            device?.run_dispose?.();
        }
    }

    HideLivePreviewsAsync(_params, invocation) {
        if (this._isThumbnailCallerAuthorized(invocation))
            this._destroyLivePreviews();
        invocation.return_value(null);
    }

    _destroyLivePreviews({
        publishPointerOutside = true,
        preserveSession = false,
    } = {}) {
        if (!preserveSession) {
            this._previewSessionOpen = false;
            this._destroyPreviewReplacementShield();
        }

        if (this._livePreviewOverlay) {
            this._livePreviewOverlay.remove_all_transitions();
            this._livePreviewOverlay.destroy();
            this._livePreviewOverlay = null;
        }

        this._livePreviewRects = [];
        if (publishPointerOutside)
            this._publishPreviewPointerInside(true);
    }

    _releasePreviewReplacementShield() {
        if (!this._previewReplacementShield)
            return;

        if (this._previewReplacementShieldReleaseSource)
            GLib.source_remove(this._previewReplacementShieldReleaseSource);

        this._previewReplacementShieldReleaseSource = GLib.timeout_add(
            GLib.PRIORITY_DEFAULT,
            PREVIEW_REPLACEMENT_REVEAL_DELAY_MS,
            () => {
                this._previewReplacementShieldReleaseSource = 0;
                const shield = this._previewReplacementShield;
                if (!shield || shield.is_destroyed?.())
                    return GLib.SOURCE_REMOVE;

                shield.remove_all_transitions();
                shield.ease({
                    opacity: 0,
                    duration: PREVIEW_REPLACEMENT_CROSSFADE_MS,
                    mode: Clutter.AnimationMode.EASE_OUT_QUAD,
                    onComplete: () => {
                        if (this._previewReplacementShield === shield)
                            this._previewReplacementShield = null;
                        if (!shield.is_destroyed?.())
                            shield.destroy();
                    },
                });
                return GLib.SOURCE_REMOVE;
            });
    }

    _destroyPreviewReplacementShield() {
        if (this._previewReplacementShieldReleaseSource) {
            GLib.source_remove(this._previewReplacementShieldReleaseSource);
            this._previewReplacementShieldReleaseSource = 0;
        }

        if (this._previewReplacementShield) {
            this._previewReplacementShield.remove_all_transitions();
            this._previewReplacementShield.destroy();
            this._previewReplacementShield = null;
        }
    }

    _isTrackable(window) {
        return Boolean(window &&
            !this._isDockWindow(window) &&
            !this._isAuxiliaryWindow(window) &&
            !window.is_override_redirect() &&
            TRACKABLE_TYPES.has(window.get_window_type()) &&
            this._windowId(window));
    }

    _applicationId(window) {
        const app = this._tracker.get_window_app(window);
        const trackedId = app?.get_id() || '';
        if (trackedId && !isSyntheticApplicationId(trackedId))
            return trackedId;

        for (const candidate of [
            window.get_gtk_application_id(),
            window.get_wm_class_instance(),
            window.get_wm_class(),
        ]) {
            if (candidate && !isSyntheticApplicationId(candidate))
                return candidate;
        }

        // Shell creates window:<stable-sequence> identities for unmatched
        // applications. They are session-local window handles, not
        // application identities, so do not expose them as dock items.
        return '';
    }

    _workspaceNumber(window) {
        if (window.is_on_all_workspaces())
            return null;
        return window.get_workspace()?.index() + 1 || null;
    }

    _isApplicationAuxiliary(window) {
        if (!window?.skip_taskbar || window.is_above?.() !== true)
            return false;

        const applicationId = this._applicationId(window);
        const processId = Number(window.get_pid?.()) || 0;

        return global.get_window_actors().some(actor => {
            const candidate = actor.meta_window;
            if (candidate === window ||
                !this._isTrackable(candidate) ||
                candidate.skip_taskbar)
                return false;

            const sameApplication = applicationId &&
                this._applicationId(candidate) === applicationId;
            const sameProcess = processId > 0 &&
                Number(candidate.get_pid?.()) === processId;
            return sameApplication || sameProcess;
        });
    }

    _windowPayload(window) {
        const rect = window.get_frame_rect();
        const workspace = this._workspaceNumber(window);
        const applicationId = this._applicationId(window);
        const onCurrentDesktop = window.is_on_all_workspaces() ||
            window.located_on_workspace(global.workspace_manager.get_active_workspace());

        return encodeList([
            this._windowId(window),
            applicationId,
            window.get_title() || '',
            applicationId,
            integerText(window.get_pid()),
            booleanText(window.minimized),
            booleanText(window.get_maximized() !== Meta.MaximizeFlags.NONE),
            booleanText(window.skip_taskbar),
            integerText(rect.x),
            integerText(rect.y),
            integerText(rect.width),
            integerText(rect.height),
            '',
            workspace === null ? '' : encodeList([String(workspace)]),
            workspace === null ? '' : encodeList([workspace]),
            booleanText(onCurrentDesktop),
            booleanText(this._isApplicationAuxiliary(window)),
        ]);
    }

    _stackedWindows() {
        return global.get_window_actors()
            .map(actor => actor.meta_window)
            .filter(window => this._isTrackable(window));
    }

    _stackingOrder() {
        return encodeList(this._stackedWindows().map(window => this._windowId(window)));
    }

    _activeWindowId() {
        const window = global.display.focus_window;
        return this._isTrackable(window) ? this._windowId(window) : '';
    }

    _trackWindow(window) {
        if (!this._isTrackable(window))
            return false;

        const id = this._windowId(window);
        if (this._windows.has(id))
            return true;

        this._windows.set(id, window);
        const iconGeometry = this._iconGeometries.get(id);
        if (iconGeometry) {
            this._setIconGeometry(
                id,
                iconGeometry.x,
                iconGeometry.y,
                iconGeometry.width,
                iconGeometry.height);
        }
        const publish = () => this._publishWindow(window);
        const signals = [
            ['position-changed', publish],
            ['size-changed', publish],
            ['workspace-changed', publish],
            ['notify::title', publish],
            ['notify::minimized', publish],
            ['notify::skip-taskbar', publish],
            ['notify::above', publish],
            ['notify::maximized-horizontally', publish],
            ['notify::maximized-vertically', publish],
            ['notify::wm-class', publish],
            ['unmanaged', () => this._onWindowRemoved(window)],
        ];

        const ids = [];
        for (const [signal, callback] of signals) {
            try {
                ids.push(window.connect(signal, callback));
            } catch (_error) {
                // Mutter versions expose a slightly different notify set.
            }
        }
        this._windowSignals.set(window, ids);
        return true;
    }

    _untrackWindow(window) {
        for (const id of this._windowSignals.get(window) || []) {
            try {
                window.disconnect(id);
            } catch (_error) {
                // The Meta.Window may already be finalized.
            }
        }
        this._windowSignals.delete(window);
        this._windows.delete(this._windowId(window));
    }

    _onWindowAdded(window) {
        this._considerDockWindow(window);
        if (!this._trackWindow(window))
            return;
        this._publishWindow(window);
        this._publishStackingOrder();
    }

    _onWindowRemoved(window) {
        const id = this._windowId(window);
        if (!this._windows.has(id))
            return;

        this._untrackWindow(window);
        this._publish('PublishWindowRemoved', '(ss)', [this._nextRevision(), id]);
        this._publishStackingOrder();
    }

    _publishWindow(window) {
        if (!this._isTrackable(window) || !this._windows.has(this._windowId(window)))
            return;
        this._publish('PublishWindow', '(ss)', [this._nextRevision(), this._windowPayload(window)]);
    }

    _publishAllWindows() {
        for (const window of this._windows.values())
            this._publishWindow(window);
    }

    _publishActiveWindow() {
        const id = this._activeWindowId();
        if (!id)
            return;
        this._publish('PublishActiveWindow', '(ss)', [this._nextRevision(), id]);
    }

    _publishStackingOrder() {
        this._publish('PublishStackingOrder', '(ss)', [this._nextRevision(), this._stackingOrder()]);
    }

    _publishCurrentDesktop() {
        const number = global.workspace_manager.get_active_workspace_index() + 1;
        this._publish('PublishCurrentDesktop', '(ssi)', [
            this._nextRevision(), String(number), number,
        ]);
    }

    _publishSnapshot() {
        const revision = this._nextRevision();
        this._call('BeginSnapshot', '(s)', [revision]);

        for (const window of this._stackedWindows()) {
            this._trackWindow(window);
            this._call('StageWindow', '(ss)', [revision, this._windowPayload(window)]);
        }

        this._call('CommitSnapshot', '(sss)', [
            revision, this._activeWindowId(), this._stackingOrder(),
        ]);
    }

    _publishAnimationOnlySnapshot() {
        const revision = this._nextRevision();
        this._call('BeginSnapshot', '(s)', [revision]);
        this._call('CommitSnapshot', '(sss)', [revision, '', '']);
    }

    _waitForCommands() {
        while (this._connected && this._pendingWaits < 2) {
            this._pendingWaits++;
            this._call('WaitForCommand', null, null, (reply) => {
                this._pendingWaits--;
                if (!this._connected || !reply)
                    return;
                this._waitForCommands();
                this._executeCommand(reply[0], reply[1], reply[2]);
            });
        }
    }

    _executeCommand(command, encodedId, state) {
        const ids = command === 'present' || command === 'hide'
            ? decodeList(encodedId) : [encodedId];
        const windows = ids.map(id => this._windows.get(id)).filter(Boolean);
        if (windows.length === 0)
            return;

        if (command === 'hide') {
            for (const window of windows)
                window.minimize();
            return;
        }

        if (command === 'present') {
            for (const window of windows) {
                window.unminimize();
                window.raise();
            }
            Main.activateWindow(
                windows.at(-1), global.get_current_time());
            return;
        }

        const window = windows[0];
        if (command === 'activate') {
            Main.activateWindow(window, global.get_current_time());
        } else if (command === 'raise') {
            window.raise();
        } else if (command === 'close') {
            window.delete(global.get_current_time());
        } else if (command === 'set-minimized') {
            state ? window.minimize() : window.unminimize();
        } else if (command === 'set-maximized') {
            state ? window.maximize(Meta.MaximizeFlags.BOTH) :
                window.unmaximize(Meta.MaximizeFlags.BOTH);
        }
    }
}
