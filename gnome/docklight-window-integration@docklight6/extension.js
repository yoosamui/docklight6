import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Meta from 'gi://Meta';
import Shell from 'gi://Shell';

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const SERVICE = 'org.docklight6.WindowIntegration';
const PATH = '/org/docklight6/WindowIntegration';
const IFACE = 'org.docklight6.WindowIntegration1';
const PROTOCOL_VERSION = '7';

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
        this._pendingWaits = 0;

        this._connect(global.display, 'window-created', (_display, window) => {
            this._onWindowAdded(window);
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

        for (const window of this._stackedWindows())
            this._trackWindow(window);

        this._nameWatch = Gio.bus_watch_name(
            Gio.BusType.SESSION,
            SERVICE,
            Gio.BusNameWatcherFlags.NONE,
            () => this._register(),
            () => this._disconnectBackend());
        this._register();
    }

    disable() {
        this._enabled = false;
        this._connected = false;

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
            this._call('Unregister', null, null, () => {});
            this._proxy = null;
        }

        this._windows = null;
        this._windowSignals = null;
        this._tracker = null;
    }

    _connect(object, signal, callback) {
        const id = object.connect(signal, callback);
        this._signals.push([object, id]);
    }

    _call(method, signature, values, callback = () => {}) {
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
        if (!this._proxy || this._connected || this._registering)
            return;

        this._registering = true;
        this._call('Register', '(s)', [PROTOCOL_VERSION], (reply) => {
            this._registering = false;
            this._connected = Boolean(reply?.[0]);
            if (!this._connected)
                return;

            this._revision = 0;
            this._pendingWaits = 0;
            this._publishSnapshot();
            this._publishCurrentDesktop();
            this._waitForCommands();
        });
    }

    _disconnectBackend() {
        this._connected = false;
        this._registering = false;
        this._pendingWaits = 0;
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
