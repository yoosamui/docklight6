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
    calculateDockRevealRect,
    calculateDockStrut,
    clampAuxiliaryToWorkArea,
    isDockPlacementCommitted,
    isPointerInsideDockInterior,
    isSyntheticApplicationId,
    parseAuxiliaryPosition,
    placeDockInWorkArea,
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
const DOCK_HIDE_ANIMATION_MS = 180;
const DOCK_REVEAL_ANIMATION_MS = 220;
// Browser PiP surfaces commonly reserve a double-click for maximizing the
// player. The preview bridge must not turn a stress-click burst into that
// window-management gesture.
const PREVIEW_DOUBLE_CLICK_GUARD_US = 500000;
const PREVIEW_DOUBLE_CLICK_DISTANCE_PX = 12;

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
        this._dockHidden = false;
        this._signals = [];
        this._connect(this._proxy, 'g-signal',
            (_proxy, _sender, signalName, parameters) => {
                if (signalName === 'DockPlacementGeometryChanged')
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
        this._dockVisibilityState = 'visible';
        this._dockPointerInside = null;
        this._pointerPosition = null;
        this._cursorTracker = global.backend.get_cursor_tracker();
        this._pointerPollSource = 0;
        this._auxiliaryWindowSignals = new Map();
        this._auxiliaryTransitions = new Map();
        this._dialogWindowSignals = new Map();
        this._configurationReloadSource = 0;
        this._dockStrut = null;
        this._dockRevealActor = null;
        this._dockRevealSignal = 0;
        this._nativeWorkAreas = [];
        this._livePreviewOverlay = null;
        this._livePreviewActors = [];
        this._livePreviewRects = [];
        this._previewPointerInside = null;
        this._previewPointerDevice = null;
        this._previewPointerSources = new Set();
        this._previewPointerPressed = false;
        this._previewPointerRestorePosition = null;
        this._previewPointerLastClick = null;
        this._previewInputSuppressedActors = [];
        this._previewInputUntrackedActors = [];
        this._previewSelectorFill =
            'rgba(105, 170, 255, 0.32)';
        this._previewSelectorOutline =
            'rgba(105, 170, 255, 0.95)';

        this._thumbnailDbus = Gio.DBusExportedObject.wrapJSObject(
            THUMBNAIL_IFACE, this);
        this._thumbnailDbus.export(Gio.DBus.session, THUMBNAIL_PATH);
        this._thumbnailNameId = Gio.bus_own_name_on_connection(
            Gio.DBus.session,
            THUMBNAIL_SERVICE,
            Gio.BusNameOwnerFlags.NONE,
            null,
            null);

        this._loadDockPlacement();
        this._refreshNativeWorkAreas();
        this._watchDockConfiguration();
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

        this._connect(global.display, 'window-created', (_display, window) => {
            this._onWindowAdded(window);
        });
        this._connect(global.window_manager, 'map', (_windowManager, actor) => {
            const window = actor?.meta_window;
            if (!window)
                return;

            // Auxiliary GTK toplevels are subject to Mutter's provisional
            // centred placement too. Hide their first actor before any
            // classification work can expose it.
            if (this._isAuxiliaryWindow(window))
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
        this._connect(global.display, 'window-demands-attention', (_display, window) => {
            this._publishWindow(window);
        });
        this._connect(global.display, 'notify::focus-window', () => {
            this._publishActiveWindow();
        });
        this._connect(global.display, 'restacked', () => {
            this._enforceDockWindowLayer();
            this._publishStackingOrder();
        });
        this._connect(global.workspace_manager, 'active-workspace-changed', () => {
            this._publishCurrentDesktop();
            this._publishAllWindows();
        });
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
        for (const window of [...this._dialogWindowSignals.keys()])
            this._clearDialogWindow(window);

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

        for (const window of [...this._windowSignals.keys()])
            this._untrackWindow(window);

        if (this._proxy) {
            this._call('Unregister', null, null, () => { });
            this._proxy = null;
        }

        this._windows = null;
        this._windowSignals = null;
        this._tracker = null;
        this._cursorTracker = null;
    }

    _loadDockPlacement() {
        let autohide = 'none';
        let alignment = 'center';

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
                const configuredAlignment = keyFile.get_string('dock', 'alignment').trim();
                if (['start', 'center', 'end', 'fill'].includes(configuredAlignment))
                    alignment = configuredAlignment;
            } catch (_error) {
                // Missing and empty alignment values use the centred default.
            }
        } catch (_error) {
            // Missing keys and a missing first-run file both mean defaults.
        }

        const changed = this._dockAutohide !== autohide ||
            this._dockAlignment !== alignment;
        this._dockAutohide = autohide;
        this._dockAlignment = alignment;
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
                        if (this._loadDockPlacement()) {
                            this._scheduleDockPlacement();
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
        if (this._considerDialogWindow(window))
            return;
        if (this._considerAuxiliaryWindow(window))
            return;

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
            const isAuxiliary = this._considerAuxiliaryWindow(window);
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
        if (!this._isDockDialog(window))
            return false;

        if (!this._dialogWindowSignals.has(window)) {
            const signals = [];
            for (const [signal, callback] of [
                ['size-changed', () => this._placeDialogWindow(window)],
                ['position-changed', () => this._placeDialogWindow(window)],
                ['notify::monitor', () => this._placeDialogWindow(window)],
                ['unmanaged', () => this._clearDialogWindow(window)],
            ]) {
                try {
                    signals.push(window.connect(signal, callback));
                } catch (_error) {
                    // Signal availability differs between Mutter releases.
                }
            }
            this._dialogWindowSignals.set(window, signals);
        }
        this._placeDialogWindow(window);
        return true;
    }

    _placeDialogWindow(window) {
        // Do not call Meta.Window.move_frame() for a GTK Wayland dialog.
        // Mutter 16 can follow the surface's transient/subsurface parent to
        // a null Meta.Window and crash the whole Shell. Moving the compositor
        // actor keeps painting and picking at the requested coordinates
        // without mutating Mutter's native window geometry.
        const actor = window?.get_compositor_private?.();
        if (!actor)
            return;

        const monitorIndex = this._dockMonitorIndex();
        const area = this._workAreaForMonitor(monitorIndex);
        if (!area)
            return;
        const rect = window.get_frame_rect();
        const x = Math.round(area.x + (area.width - rect.width) / 2);
        const y = Math.round(area.y + (area.height - rect.height) / 2);
        actor.remove_all_transitions();
        actor.scale_x = 1;
        actor.scale_y = 1;
        actor.translation_x = x - rect.x;
        actor.translation_y = y - rect.y;
    }

    _clearDialogWindow(window) {
        for (const id of this._dialogWindowSignals.get(window) || []) {
            try {
                window.disconnect(id);
            } catch (_error) {
                // The Meta.Window may already be finalized.
            }
        }
        this._dialogWindowSignals.delete(window);
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
        actor.remove_all_transitions();
        actor.scale_x = 1;
        actor.scale_y = 1;
        actor.translation_x = resolvedTarget.x - rect.x;
        actor.translation_y = resolvedTarget.y - rect.y;

        // KDE's layer-shell backend places private surfaces on OVERLAY and
        // the dock on TOP.  GNOME has no layer-shell, so both keep-above
        // windows enter Mutter's TOP stack layer.  Raise the later private
        // surface through Meta.Window so Mutter preserves the same ordering;
        // changing WindowActor sibling order is only temporary and is undone
        // by the compositor's next restack.
        try {
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
            transition.actor.set_opacity(transition.opacity);
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
        this._enforceDockWindowLayer();
        this._updateDockRevealActor();
        this._beginDockTransition();

        for (const [signal, callback] of [
            ['position-changed', () => {
                // Mutter can apply its ordinary-toplevel initial placement
                // after window-created. Reassert the configured edge when
                // that late placement moves the dock back to the centre.
                this._beginDockTransition();
                this._scheduleDockPlacement(
                    true, DOCK_PLACEMENT_DELAY_MS);
            }],
            ['size-changed', () => {
                this._scheduleDockPlacement(this._dockTransitioning);
            }],
            ['notify::monitor', () => {
                this._beginDockTransition();
                this._scheduleDockPlacement(true);
            }],
            ['unmanaged', () => this._clearDockWindow()],
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
            this._dockWindow.make_above();
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

    _clearDockWindow() {
        this._finishDockTransition();
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

    _beginDockTransition() {
        if (!this._dockWindow)
            return;

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

        const base = this._dockActorBaseTranslation ?? {x: 0, y: 0};
        const positioned = placeDockInWorkArea(
            Main.layoutManager.monitors[this._dockMonitorIndex()],
            this._workAreaForMonitor(this._dockMonitorIndex()),
            placement,
            this._dockAlignment);
        const offset = {x: 0, y: 0};
        if (positioned.edge === 'top')
            offset.y = -positioned.height;
        else if (positioned.edge === 'bottom')
            offset.y = positioned.height;
        else if (positioned.edge === 'left')
            offset.x = -positioned.width;
        else
            offset.x = positioned.width;

        const serial = ++this._dockVisibilityAnimationSerial;
        actor.remove_all_transitions();
        if (!hidden && actor.get_opacity() === 0 &&
            actor.translation_x === base.x &&
            actor.translation_y === base.y) {
            actor.translation_x = base.x + offset.x;
            actor.translation_y = base.y + offset.y;
        }
        const startX = actor.translation_x;
        const startY = actor.translation_y;
        const targetX = hidden ? base.x + offset.x : base.x;
        const targetY = hidden ? base.y + offset.y : base.y;
        const fullDistance = Math.max(1, Math.hypot(offset.x, offset.y));
        const remainingFraction = Math.min(1,
            Math.hypot(targetX - startX, targetY - startY) / fullDistance);
        const fullDuration = hidden
            ? DOCK_HIDE_ANIMATION_MS
            : DOCK_REVEAL_ANIMATION_MS;
        const remainingDistance = Math.hypot(
            targetX - startX, targetY - startY);
        const duration = Math.max(60,
            Math.round(fullDuration * remainingFraction));

        this._dockVisibilityState = hidden ? 'hiding' : 'revealing';
        actor.set_opacity(this._dockActorOpacity);
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
            actor.translation_x = targetX;
            actor.translation_y = targetY;
            actor.set_opacity(hidden ? 0 : this._dockActorOpacity);
            this._updateDockRevealActor();
            this._publishDockPointerInside(true);
            this._call(
                'PublishDockAnimationCompleted', '(b)', [hidden]);
        };

        // Clutter does not guarantee an onComplete callback when every
        // animated property already equals its target. Complete that state
        // change here so the C++ controller never waits forever for an
        // animation that was not created.
        if (remainingDistance < 0.5) {
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
            translation_x: targetX,
            translation_y: targetY,
            duration,
            mode: hidden
                ? Clutter.AnimationMode.EASE_IN_QUAD
                : Clutter.AnimationMode.EASE_OUT_QUAD,
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
            this._dockAlignment);
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

        return this._livePreviewRects.some(rect =>
            this._pointerPosition.x >= rect.x &&
            this._pointerPosition.x < rect.x + rect.width &&
            this._pointerPosition.y >= rect.y &&
            this._pointerPosition.y < rect.y + rect.height);
    }

    _updateLivePreviewSelectors(force = false) {
        for (const rect of this._livePreviewRects) {
            const selected = Boolean(this._pointerPosition &&
                this._pointerPosition.x >= rect.x &&
                this._pointerPosition.x < rect.x + rect.width &&
                this._pointerPosition.y >= rect.y &&
                this._pointerPosition.y < rect.y + rect.height);
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
                } else if (this._dockTransitioning &&
                    ++this._dockPlacementAttempts <
                        DOCK_PLACEMENT_MAX_ATTEMPTS) {
                    // move_frame() is asynchronous on Wayland. Keep the
                    // provisional frame out of the scene until Mutter's
                    // frame rectangle (plus any strut compensation) proves
                    // that the painted dock has reached the requested edge.
                    this._scheduleDockPlacement(
                        false, DOCK_PLACEMENT_DELAY_MS);
                } else {
                    // Do not leave DockLight permanently invisible if a
                    // compositor rejects movement or a window disappears.
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

        this._dockPlacement = placement;
        this._updateDockRevealActor();
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
            this._dockAlignment);
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
        if (!this._dockPlacement)
            return Main.layoutManager.primaryIndex;

        const rect = this._dockPlacement;
        let bestIndex = Main.layoutManager.primaryIndex;
        let bestArea = -1;
        for (let index = 0; index < Main.layoutManager.monitors.length; index++) {
            const monitor = Main.layoutManager.monitors[index];
            const overlapWidth = Math.max(0,
                Math.min(rect.x + rect.width, monitor.x + monitor.width) -
                Math.max(rect.x, monitor.x));
            const overlapHeight = Math.max(0,
                Math.min(rect.y + rect.height, monitor.y + monitor.height) -
                Math.max(rect.y, monitor.y));
            const area = overlapWidth * overlapHeight;
            if (area > bestArea) {
                bestArea = area;
                bestIndex = index;
            }
        }
        return bestIndex;
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
            this._dockAlignment);
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

            this._publishSnapshot();
            this._publishCurrentDesktop();
            this._waitForCommands();
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
        this._connected = false;
        this._registering = false;
        this._pendingWaits = 0;
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

    ShowLivePreviewsAsync(params, invocation) {
        const [previews] = params;
        if (!this._isThumbnailCallerAuthorized(invocation)) {
            invocation.return_value(null);
            return;
        }

        // Do not publish a transient outside state while replacing one set of
        // previews with another. The new rectangles are installed below and
        // immediately reconciled against Shell's compositor pointer.
        this._destroyLivePreviews(false);

        // WindowPreviewLayout actors must remain in Shell's UI layer. Making
        // them children of a real MetaWindowActor creates an unsupported
        // paint dependency and can stop live video damage from reaching the
        // clones. The full-stage container and layout-created descendants
        // remain non-reactive; each thumbnail container handles its own click
        // and forwards the selected window id through the integration service.
        const overlay = new Clutter.Actor({
            reactive: false,
            x: 0,
            y: 0,
            width: global.stage.width,
            height: global.stage.height,
        });

        let previewCount = 0;
        const previewRects = [];
        const previewActors = [];
        const previewFadeActors = [];
        for (const [windowId, x, y, width, height] of previews) {
            if (width <= 0 || height <= 0)
                continue;

            const window = this._windows.get(windowId);
            const windowActor = window?.get_compositor_private?.();
            if (!window || !windowActor || windowActor.is_destroyed?.())
                continue;

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
                opacity: 0,
                x: previewOffsetX,
                y: previewOffsetY,
                width: previewWidth,
                height: previewHeight,
            });

            // WindowPreviewLayout tracks its container during assignment, so
            // match GNOME Shell's own construction order instead of passing
            // it as a construct property.
            preview.layout_manager = layout;
            // The compositor clone owns pointer input, so the GTK card below
            // cannot paint its normal mouse-over selection. Make the selector
            // the card-sized clone parent/background and drive it from
            // Shell's authoritative pointer position. An opaque backing sits
            // between the selector and the alpha-bearing window clone, so the
            // selector can show in card margins but never tint image pixels.
            const selector = new St.Widget({
                reactive: true,
                x,
                y,
                width,
                height,
                style: 'background-color: transparent; ' +
                    'border-radius: 6px;',
            });
            const thumbnailBacking = new St.Widget({
                reactive: false,
                x: previewOffsetX,
                y: previewOffsetY,
                width: previewWidth,
                height: previewHeight,
                style: 'background-color: rgb(28, 28, 32);',
            });
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
            selector.add_child(thumbnailBacking);
            selector.add_child(preview);
            selector.add_child(selectionOutline);
            selector.set_child_above_sibling(preview, thumbnailBacking);
            selector.set_child_above_sibling(selectionOutline, preview);
            overlay.add_child(selector);
            layout.add_window(window);
            let primaryButtonPressed = false;
            selector.connect('button-press-event', (_actor, event) => {
                if (event.get_button() !== 1)
                    return Clutter.EVENT_PROPAGATE;

                // Consume the complete click in Shell. Otherwise the press
                // can propagate to the GTK preview surface while the release
                // lands on this compositor clone, so neither side observes a
                // usable click.
                primaryButtonPressed = true;
                return Clutter.EVENT_STOP;
            });
            selector.connect('button-release-event', (_actor, event) => {
                if (event.get_button() !== 1)
                    return Clutter.EVENT_PROPAGATE;

                // The GTK preview is mapped before this compositor clone's
                // input region is committed. A press can therefore reach GTK
                // while its release reaches Shell. Never let that orphaned
                // release fall through and become a second GTK action.
                if (!primaryButtonPressed) {
                    // The press reached the GTK card before this actor entered
                    // Shell's input region. The release is nevertheless the
                    // first safe point at which Mutter's implicit pointer grab
                    // has ended, so use it to complete an auxiliary/PiP click.
                    if (this._isApplicationAuxiliary(window))
                        this._forwardPreviewPrimaryClick(window, preview, event);
                    return Clutter.EVENT_STOP;
                }

                primaryButtonPressed = false;
                if (this._isApplicationAuxiliary(window))
                    this._forwardPreviewPrimaryClick(window, preview, event);
                else
                    this._call(
                        'ActivatePreviewWindow',
                        '(s)',
                        [windowId]);
                return Clutter.EVENT_STOP;
            });
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
            previewActors.push(selector);
            previewFadeActors.push(preview);
            previewCount++;
        }

        if (previewCount > 0) {
            Main.uiGroup.add_child(overlay);
            this._livePreviewOverlay = overlay;
            this._livePreviewActors = previewActors;
            this._livePreviewRects = previewRects;
            // Adding an actor to uiGroup is sufficient for painting, but it
            // does not add that actor to Mutter's stage input region. Track
            // each thumbnail (rather than the full-stage overlay) so Shell
            // receives its button events without intercepting input anywhere
            // outside the visible previews.
            for (const preview of previewActors) {
                Main.layoutManager.trackChrome(preview, {
                    affectsStruts: false,
                    affectsInputRegion: true,
                    trackFullscreen: true,
                });
            }
            this._publishPreviewPointerInside(true);
            // Fading the full-stage overlay makes Mutter allocate and blend
            // a screen-sized offscreen surface just to reveal thumbnail-sized
            // clones. Animate each small clone instead so video keeps the
            // compositor's normal frame cadence while the preview appears.
            for (const preview of previewFadeActors) {
                preview.ease({
                    opacity: 255,
                    duration: 100,
                    mode: Clutter.AnimationMode.EASE_OUT_QUAD,
                });
            }
        } else {
            overlay.destroy();
            this._livePreviewActors = [];
            this._livePreviewRects = [];
            this._publishPreviewPointerInside(true);
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

    _forwardPreviewPrimaryClick(window, preview, event) {
        const [stageX, stageY] = event.get_coords();
        const [previewX, previewY] = preview.get_transformed_position();
        const [previewWidth, previewHeight] = preview.get_transformed_size();
        if (previewWidth <= 0 || previewHeight <= 0)
            return;

        this._forwardPreviewPrimaryClickAt(
            window,
            (stageX - previewX) / previewWidth,
            (stageY - previewY) / previewHeight);
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

        // Tracked Shell chrome contributes to the stage input region even
        // when its actor is non-reactive. Remove the thumbnail selectors
        // from that region for the short virtual-click sequence, but leave
        // them painted so forwarding a PiP click cannot flash the preview.
        this._previewInputUntrackedActors = [
            ...this._livePreviewActors,
        ];
        for (const actor of this._previewInputUntrackedActors) {
            try {
                Main.layoutManager.untrackChrome(actor);
            } catch (_error) {
                // A replacement preview can already have untracked it.
            }
        }

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

        for (const actor of this._previewInputUntrackedActors) {
            try {
                if (!actor.is_destroyed?.() &&
                    this._livePreviewActors.includes(actor)) {
                    Main.layoutManager.trackChrome(actor, {
                        affectsStruts: false,
                        affectsInputRegion: true,
                        trackFullscreen: true,
                    });
                }
            } catch (_error) {
                // A preview can be replaced during the injected click.
            }
        }
        this._previewInputUntrackedActors = [];
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

    _destroyLivePreviews(publishPointerOutside = true) {
        for (const preview of this._livePreviewActors) {
            try {
                Main.layoutManager.untrackChrome(preview);
            } catch (_error) {
                // Shell teardown may already have dropped tracked actors.
            }
        }
        this._livePreviewActors = [];

        if (this._livePreviewOverlay) {
            this._livePreviewOverlay.remove_all_transitions();
            this._livePreviewOverlay.destroy();
            this._livePreviewOverlay = null;
        }

        this._livePreviewRects = [];
        if (publishPointerOutside)
            this._publishPreviewPointerInside(true);
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
            windows.at(-1).activate(global.get_current_time());
            return;
        }

        const window = windows[0];
        if (command === 'activate') {
            window.activate(global.get_current_time());
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
