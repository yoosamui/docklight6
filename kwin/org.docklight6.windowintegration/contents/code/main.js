(function () {
    "use strict";

    const SERVICE_NAME =
        "org.docklight6.WindowIntegration";
    const OBJECT_PATH =
        "/org/docklight6/WindowIntegration";
    const INTERFACE_NAME =
        "org.docklight6.WindowIntegration1";
    const PROTOCOL_VERSION = "4";

    let connected = false;
    let registering = false;
    let waitingForCommand = false;
    let revision = 0;

    const trackedWindows = {};
    let dockSurface = null;

    function windowId(window) {
        if (!window || !window.internalId)
            return "";

        if (typeof window.internalId.toString ===
            "function") {
            return String(
                window.internalId.toString());
        }

        return String(window.internalId);
    }

    function isTrackable(window) {
        return Boolean(
            window &&
            window.managed &&
            !window.deleted &&
            !window.popupWindow &&
            !window.outline &&
            !window.desktopWindow &&
            !window.dock &&
            !isDocklightSurface(window) &&
            windowId(window));
    }

    function isDocklightSurface(window) {
        return Boolean(
            window &&
            !window.deleted &&
            !window.popupWindow &&
            !window.tooltip &&
            !window.outline &&
            !window.desktopWindow &&
            window.skipTaskbar &&
            String(
                window.resourceName || "") ===
                "docklight6");
    }

    function findDocklightSurface(
        excludedWindow) {
        const windows =
            workspace.stackingOrder || [];

        for (let index = 0;
             index < windows.length;
             ++index) {
            if (windows[index] !==
                    excludedWindow &&
                isDocklightSurface(
                    windows[index])) {
                return windows[index];
            }
        }

        return null;
    }

    function booleanText(value) {
        return value ? "1" : "0";
    }

    function integerText(value) {
        const number = Number(value);

        if (!isFinite(number))
            return "0";

        return String(Math.round(number));
    }

    function encodedList(values, transform) {
        if (!values)
            return "";

        const result = [];

        for (let index = 0;
             index < values.length;
             ++index) {
            let value = values[index];

            if (transform)
                value = transform(value);

            if (value === undefined ||
                value === null)
                continue;

            result.push(
                encodeURIComponent(
                    String(value)));
        }

        return result.join(",");
    }

    function desktopId(desktop) {
        if (!desktop)
            return "";

        if (typeof desktop.id === "function")
            return desktop.id();

        return desktop.id || "";
    }

    function iconName(window) {
        try {
            if (window.icon) {
                if (typeof window.icon.name ===
                    "function") {
                    const name =
                        window.icon.name();

                    if (name)
                        return String(name);
                }

                if (typeof window.icon.name ===
                    "string" &&
                    window.icon.name) {
                    return window.icon.name;
                }
            }
        } catch (error) {
            // Some KWin versions expose QIcon without scriptable methods.
        }

        return String(
            window.desktopFileName || "");
    }

    function maximized(window) {
        if (typeof window.maximized ===
            "boolean") {
            return window.maximized;
        }

        if (typeof window.maximizeMode ===
            "number") {
            return window.maximizeMode !== 0;
        }

        return false;
    }

    function nextRevision() {
        ++revision;
        return String(revision);
    }

    function handlePublishReply(accepted) {
        if (accepted === true)
            return;

        connected = false;
        waitingForCommand = false;
        registerIntegration();
    }

    function callWindowMethod(
        methodName,
        messageRevision,
        window) {
        const geometry =
            window.frameGeometry || {};
        const payload =
            encodedList([
                windowId(window),
                String(
                    window.desktopFileName || ""),
                String(window.caption || ""),
                iconName(window),
                integerText(window.pid),
                booleanText(window.minimized),
                booleanText(maximized(window)),
                booleanText(window.skipTaskbar),
                integerText(geometry.x),
                integerText(geometry.y),
                integerText(geometry.width),
                integerText(geometry.height),
                encodedList(window.activities),
                encodedList(
                    window.desktops,
                    desktopId)
            ]);

        callDBus(
            SERVICE_NAME,
            OBJECT_PATH,
            INTERFACE_NAME,
            methodName,
            messageRevision,
            payload,
            handlePublishReply);
    }

    function stackingOrder() {
        const identifiers = [];
        const windows =
            workspace.stackingOrder || [];

        for (let index = 0;
             index < windows.length;
             ++index) {
            if (isTrackable(windows[index])) {
                identifiers.push(
                    windowId(windows[index]));
            }
        }

        return encodedList(identifiers);
    }

    function activeWindowId() {
        return isTrackable(
            workspace.activeWindow)
            ? windowId(workspace.activeWindow)
            : "";
    }

    function publishWindow(window) {
        if (!connected) {
            registerIntegration();
            return;
        }

        if (!isTrackable(window))
            return;

        callWindowMethod(
            "PublishWindow",
            nextRevision(),
            window);
    }

    function publishActiveWindow() {
        if (!connected) {
            registerIntegration();
            return;
        }

        callDBus(
            SERVICE_NAME,
            OBJECT_PATH,
            INTERFACE_NAME,
            "PublishActiveWindow",
            nextRevision(),
            activeWindowId(),
            handlePublishReply);
    }

    function publishStackingOrder() {
        if (!connected) {
            registerIntegration();
            return;
        }

        callDBus(
            SERVICE_NAME,
            OBJECT_PATH,
            INTERFACE_NAME,
            "PublishStackingOrder",
            nextRevision(),
            stackingOrder(),
            handlePublishReply);
    }

    function publishDockSurfaceGeometry() {
        if (!connected) {
            registerIntegration();
            return;
        }

        const geometry =
            dockSurface &&
            isDocklightSurface(dockSurface)
                ? dockSurface.frameGeometry || {}
                : {};

        callDBus(
            SERVICE_NAME,
            OBJECT_PATH,
            INTERFACE_NAME,
            "PublishDockSurfaceGeometry",
            nextRevision(),
            Number(geometry.x || 0),
            Number(geometry.y || 0),
            Number(geometry.width || 0),
            Number(geometry.height || 0),
            handlePublishReply);
    }

    function connectDockSurface(window) {
        if (!isDocklightSurface(window))
            return false;

        if (dockSurface === window)
            return true;

        if (isDocklightSurface(dockSurface))
            return false;

        dockSurface = window;

        connectSignal(
            window.frameGeometryChanged,
            function () {
                if (dockSurface === window)
                    publishDockSurfaceGeometry();
            });

        return true;
    }

    function executeCommand(
        command,
        identifier,
        state) {
        const window =
            trackedWindows[identifier];

        if (!window ||
            !isTrackable(window))
            return;

        if (command === "activate") {
            workspace.activateWindow(window);
        } else if (command === "raise") {
            workspace.raiseWindow(window);
        } else if (command === "close") {
            window.closeWindow();
        } else if (
            command === "set-minimized") {
            window.minimized =
                state === true;
        } else if (
            command === "set-maximized") {
            window.setMaximize(
                state === true,
                state === true);
        }
    }

    function waitForCommand() {
        if (!connected ||
            waitingForCommand)
            return;

        waitingForCommand = true;

        callDBus(
            SERVICE_NAME,
            OBJECT_PATH,
            INTERFACE_NAME,
            "WaitForCommand",
            function (
                command,
                identifier,
                state) {
                waitingForCommand = false;

                if (!connected)
                    return;

                try {
                    executeCommand(
                        command,
                        identifier,
                        state);
                } catch (error) {
                    print(
                        "Docklight command failed:",
                        command,
                        identifier,
                        String(error));
                } finally {
                    // A failed KWin operation must not terminate the
                    // long-running command channel.
                    waitForCommand();
                }
            });
    }

    function connectSignal(
        signal,
        callback) {
        if (signal &&
            typeof signal.connect === "function") {
            signal.connect(callback);
        }
    }

    function connectWindow(window) {
        if (!isTrackable(window))
            return false;

        const identifier = windowId(window);

        if (trackedWindows[identifier])
            return true;

        trackedWindows[identifier] = window;

        const publish =
            function () {
                publishWindow(window);
            };

        connectSignal(
            window.frameGeometryChanged,
            publish);
        connectSignal(
            window.skipTaskbarChanged,
            publish);
        connectSignal(
            window.iconChanged,
            publish);
        connectSignal(
            window.desktopsChanged,
            publish);
        connectSignal(
            window.activitiesChanged,
            publish);
        connectSignal(
            window.minimizedChanged,
            publish);
        connectSignal(
            window.captionChanged,
            publish);
        connectSignal(
            window.maximizedChanged,
            publish);
        connectSignal(
            window.desktopFileNameChanged,
            publish);
        connectSignal(
            window.stackingOrderChanged,
            publishStackingOrder);

        return true;
    }

    function publishSnapshot() {
        const snapshotRevision =
            nextRevision();

        callDBus(
            SERVICE_NAME,
            OBJECT_PATH,
            INTERFACE_NAME,
            "BeginSnapshot",
            snapshotRevision,
            handlePublishReply);

        const windows =
            workspace.stackingOrder || [];

        for (let index = 0;
             index < windows.length;
             ++index) {
            connectDockSurface(
                windows[index]);

            if (connectWindow(windows[index])) {
                callWindowMethod(
                    "StageWindow",
                    snapshotRevision,
                    windows[index]);
            }
        }

        callDBus(
            SERVICE_NAME,
            OBJECT_PATH,
            INTERFACE_NAME,
            "CommitSnapshot",
            snapshotRevision,
            activeWindowId(),
            stackingOrder(),
            handlePublishReply);

        publishDockSurfaceGeometry();
    }

    function registerIntegration() {
        if (connected || registering)
            return;

        registering = true;

        callDBus(
            SERVICE_NAME,
            OBJECT_PATH,
            INTERFACE_NAME,
            "Register",
            PROTOCOL_VERSION,
            function (accepted) {
                registering = false;
                connected = accepted === true;

                if (!connected) {
                    waitingForCommand = false;
                    return;
                }

                revision = 0;
                waitForCommand();
                publishSnapshot();
            });
    }

    function onWindowAdded(window) {
        const isDockSurface =
            connectDockSurface(window);

        const trackable =
            connectWindow(window);

        if (!connected) {
            registerIntegration();
            return;
        }

        if (trackable)
            publishWindow(window);

        if (isDockSurface)
            publishDockSurfaceGeometry();

        publishStackingOrder();
    }

    function onWindowRemoved(window) {
        const identifier =
            windowId(window);
        const wasTracked =
            Boolean(trackedWindows[identifier]);

        delete trackedWindows[identifier];

        const wasDockSurface =
            dockSurface === window;

        if (wasDockSurface) {
            dockSurface = null;

            connectDockSurface(
                findDocklightSurface(window));
        }

        if (!connected) {
            registerIntegration();
            return;
        }

        if (wasTracked) {
            callDBus(
                SERVICE_NAME,
                OBJECT_PATH,
                INTERFACE_NAME,
                "PublishWindowRemoved",
                nextRevision(),
                identifier,
                handlePublishReply);
        }

        if (wasDockSurface)
            publishDockSurfaceGeometry();

        publishStackingOrder();
    }

    const initialWindows =
        workspace.stackingOrder || [];

    for (let index = 0;
         index < initialWindows.length;
         ++index) {
        connectDockSurface(
            initialWindows[index]);
        connectWindow(initialWindows[index]);
    }

    connectSignal(
        workspace.windowAdded,
        onWindowAdded);
    connectSignal(
        workspace.windowRemoved,
        onWindowRemoved);
    connectSignal(
        workspace.windowActivated,
        publishActiveWindow);

    registerIntegration();
}());
