import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Meta from 'gi://Meta';
import Shell from 'gi://Shell';
import St from 'gi://St';

import { Extension } from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';

const SERVICE = 'org.docklight6.WindowIntegration';
const PATH = '/org/docklight6/WindowIntegration';
const IFACE = 'org.docklight6.WindowIntegration1';
const PROTOCOL_VERSION = '7';
const DOCK_PLACEMENT_DELAY_MS = 30;
// Docklight debounces configuration reloads for 200 ms. Keep the ordinary
// Wayland toplevel hidden past that boundary so an edge change cannot expose
// the old orientation before GTK supplies the final allocation.
const DOCK_TRANSITION_DELAY_MS = 300;
const REGISTRATION_RETRY_MS = 250;
const CONFIGURATION_SETTLE_MS = 50;

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
        this._signals = [];
        this._revision = 0;
        this._connected = false;
        this._registering = false;
        this._serviceAvailable = false;
        this._pendingWaits = 0;
        this._registrationRetrySource = 0;
        this._dockWindow = null;
        this._dockWindowSignals = [];
        this._dockPlacementSource = 0;
        this._dockDiscoverySources = new Set();
        this._dockTransitioning = false;
        this._dockActor = null;
        this._dockActorOpacity = 255;
        this._auxiliaryWindowSignals = new Map();
        this._dialogWindowSignals = new Map();
        this._configurationReloadSource = 0;
        this._dockStrut = null;
        this._nativeWorkAreas = [];

        this._loadDockPlacement();
        this._refreshNativeWorkAreas();
        this._watchDockConfiguration();

        this._connect(global.display, 'window-created', (_display, window) => {
            this._onWindowAdded(window);
        });
        this._connect(global.window_manager, 'map', (_windowManager, actor) => {
            const window = actor?.meta_window;
            if (!window)
                return;

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
            this._publishStackingOrder();
        });
        this._connect(global.workspace_manager, 'active-workspace-changed', () => {
            this._publishCurrentDesktop();
            this._publishAllWindows();
        });
        this._connect(Main.layoutManager, 'monitors-changed', () => {
            this._removeDockStrut();
            this._refreshNativeWorkAreas();
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

        if (this._registrationRetrySource) {
            GLib.source_remove(this._registrationRetrySource);
            this._registrationRetrySource = 0;
        }

        if (this._dockPlacementSource) {
            GLib.source_remove(this._dockPlacementSource);
            this._dockPlacementSource = 0;
        }
        if (this._configurationReloadSource) {
            GLib.source_remove(this._configurationReloadSource);
            this._configurationReloadSource = 0;
        }
        for (const source of this._dockDiscoverySources)
            GLib.source_remove(source);
        this._dockDiscoverySources.clear();
        this._clearDockWindow();
        this._removeDockStrut();
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
    }

    _loadDockPlacement() {
        let location = 'bottom';
        let alignment = 'center';
        let monitor = 'primary';
        let autohide = 'none';

        const path = GLib.build_filenamev([
            GLib.get_user_config_dir(), 'docklight6', 'docklight.conf',
        ]);
        const keyFile = new GLib.KeyFile();

        try {
            keyFile.load_from_file(path, GLib.KeyFileFlags.NONE);
            const configuredLocation = keyFile.get_string('dock', 'location').trim();
            const configuredAlignment = keyFile.get_string('dock', 'alignment').trim();
            const configuredMonitor = keyFile.get_string('dock', 'monitor').trim();

            if (['bottom', 'left', 'top', 'right'].includes(configuredLocation))
                location = configuredLocation;
            if (['start', 'center', 'end', 'fill'].includes(configuredAlignment))
                alignment = configuredAlignment;
            if (configuredMonitor)
                monitor = configuredMonitor;
            try {
                const configuredAutohide = keyFile.get_string('dock', 'autohide').trim();
                if (['none', 'autohide', 'intellihide'].includes(configuredAutohide))
                    autohide = configuredAutohide;
            } catch (_error) {
                // Older configuration files use the non-hiding default.
            }
        } catch (_error) {
            // Missing keys and a missing first-run file both mean defaults.
        }

        const changed = this._dockLocation !== location ||
            this._dockAlignment !== alignment ||
            this._dockMonitor !== monitor ||
            this._dockAutohide !== autohide;
        this._dockLocation = location;
        this._dockAlignment = alignment;
        this._dockMonitor = monitor;
        this._dockAutohide = autohide;
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
                            this._beginDockTransition();
                            this._scheduleDockPlacement(true);
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
            applicationId === 'org.docklight6' &&
            (type === Meta.WindowType.NORMAL ||
                type === Meta.WindowType.DOCK) &&
            !window.get_transient_for?.();
    }

    _considerDockWindow(window) {
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
        const monitorIndex = this._dockMonitorIndex();
        const area = this._workAreaForMonitor(monitorIndex);
        if (!area)
            return;
        const rect = window.get_frame_rect();
        const x = Math.round(area.x + (area.width - rect.width) / 2);
        const y = Math.round(area.y + (area.height - rect.height) / 2);
        if (rect.x !== x || rect.y !== y)
            window.move_frame(true, x, y);
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

        const match = title.match(
            /^Docklight 6 (Tooltip|Preview)@(-?\d+),(-?\d+)$/);
        if (!match)
            return null;

        return {
            x: Number.parseInt(match[2], 10),
            y: Number.parseInt(match[3], 10),
        };
    }

    _isAuxiliaryWindow(window) {
        return this._auxiliaryPosition(window) !== null;
    }

    _considerAuxiliaryWindow(window) {
        const position = this._auxiliaryPosition(window);
        if (!position)
            return false;

        if (!this._auxiliaryWindowSignals.has(window)) {
            const signals = [];
            for (const [signal, callback] of [
                ['notify::title', () => this._placeAuxiliaryWindow(window)],
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

        const rect = window.get_frame_rect();
        if (rect.x !== target.x || rect.y !== target.y)
            window.move_frame(true, target.x, target.y);
    }

    _clearAuxiliaryWindow(window) {
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
        this._beginDockTransition();

        for (const [signal, callback] of [
            ['position-changed', () => {
                // Mutter can apply its ordinary-toplevel initial placement
                // after window-created. Reassert the configured edge when
                // that late placement moves the dock back to the centre.
                this._scheduleDockPlacement();
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
    }

    _beginDockTransition() {
        if (!this._dockWindow)
            return;

        if (!this._dockTransitioning) {
            this._dockTransitioning = true;
            this._dockActorOpacity = 255;
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
        actor.set_opacity(0);
    }

    _finishDockTransition() {
        if (this._dockTransitioning && this._dockActor) {
            try {
                this._dockActor.set_opacity(this._dockActorOpacity);
            } catch (_error) {
                // The actor can disappear while the window is being closed.
            }
        }

        this._dockTransitioning = false;
        this._dockActor = null;
        this._dockActorOpacity = 255;
    }

    _scheduleDockPlacement(restart = false) {
        if (!this._enabled || !this._dockWindow)
            return;

        if (this._dockPlacementSource) {
            if (!restart)
                return;
            GLib.source_remove(this._dockPlacementSource);
            this._dockPlacementSource = 0;
        }

        const delay = this._dockTransitioning
            ? DOCK_TRANSITION_DELAY_MS
            : DOCK_PLACEMENT_DELAY_MS;

        this._dockPlacementSource = GLib.timeout_add(
            GLib.PRIORITY_DEFAULT,
            delay,
            () => {
                this._dockPlacementSource = 0;
                this._placeDockWindow();
                this._finishDockTransition();
                return GLib.SOURCE_REMOVE;
            });
    }

    _dockMonitorIndex() {
        if (this._dockMonitor === 'primary')
            return Main.layoutManager.primaryIndex;

        const current = this._dockWindow?.get_monitor?.() ?? -1;
        return current >= 0 ? current : Main.layoutManager.primaryIndex;
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
        Main.layoutManager.removeChrome(this._dockStrut);
        this._dockStrut.destroy();
        this._dockStrut = null;
    }

    _updateDockStrut(monitor, dockRect) {
        if (this._dockAutohide !== 'none') {
            this._removeDockStrut();
            return;
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

        let x = monitor.x;
        let y = monitor.y;
        let width = monitor.width;
        let height = monitor.height;
        switch (this._dockLocation) {
        case 'top':
            height = Math.max(1, dockRect.y + dockRect.height - monitor.y);
            break;
        case 'bottom':
            y = dockRect.y;
            height = Math.max(1, monitor.y + monitor.height - dockRect.y);
            break;
        case 'left':
            width = Math.max(1, dockRect.x + dockRect.width - monitor.x);
            break;
        case 'right':
            x = dockRect.x;
            width = Math.max(1, monitor.x + monitor.width - dockRect.x);
            break;
        }
        this._dockStrut.set_position(Math.round(x), Math.round(y));
        this._dockStrut.set_size(Math.round(width), Math.round(height));
    }

    _placeDockWindow() {
        const window = this._dockWindow;
        if (!window)
            return;

        const monitorIndex = this._dockMonitorIndex();
        const monitor = Main.layoutManager.monitors[monitorIndex];
        if (!monitor)
            return;
        const rect = window.get_frame_rect();
        let width = Math.max(1, rect.width);
        let height = Math.max(1, rect.height);
        let x = monitor.x;
        let y = monitor.y;

        const horizontal = this._dockLocation === 'bottom' ||
            this._dockLocation === 'top';
        if (this._dockAlignment === 'fill') {
            if (horizontal)
                width = monitor.width;
            else
                height = monitor.height;
        }

        if (horizontal) {
            if (this._dockAlignment === 'center')
                x += Math.floor((monitor.width - width) / 2);
            else if (this._dockAlignment === 'end')
                x += monitor.width - width;

            y = this._dockLocation === 'bottom'
                ? monitor.y + monitor.height - height
                : monitor.y;
        } else {
            if (this._dockAlignment === 'center')
                y += Math.floor((monitor.height - height) / 2);
            else if (this._dockAlignment === 'end')
                y += monitor.height - height;

            x = this._dockLocation === 'right'
                ? monitor.x + monitor.width - width
                : monitor.x;
        }

        x = Math.round(x);
        y = Math.round(y);
        if (this._dockAlignment === 'fill')
            window.move_resize_frame(false, x, y, width, height);
        else if (rect.x !== x || rect.y !== y)
            // This is Shell-managed dock placement, not an interactive user
            // move. A user operation makes Mutter apply its normal toplevel
            // edge constraints, adding an 8 px inset on some screen edges;
            // that inset is then exposed as a gap beside the dock strut.
            window.move_frame(false, x, y);

        this._updateDockStrut(monitor, {x, y, width, height});
        this._publishDockSurfaceGeometry({x, y, width, height});
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

            // The initial actor scan can run before Mutter has populated the
            // dock's application identity and final window metadata. Repeat
            // it after registration so an already-mapped dock is reliably
            // discovered and placed.
            for (const actor of global.get_window_actors())
                this._considerDockWindow(actor.meta_window);

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
        return app?.get_id() || window.get_gtk_application_id() ||
            window.get_wm_class_instance() || window.get_wm_class() || '';
    }

    _workspaceNumber(window) {
        if (window.is_on_all_workspaces())
            return null;
        return window.get_workspace()?.index() + 1 || null;
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
