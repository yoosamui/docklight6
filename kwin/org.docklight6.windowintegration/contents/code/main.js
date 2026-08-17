(function () {
    "use strict";

    const SERVICE_NAME =
        "org.docklight6.WindowIntegration";
    const OBJECT_PATH =
        "/org/docklight6/WindowIntegration";
    const INTERFACE_NAME =
        "org.docklight6.WindowIntegration1";
    const PROTOCOL_VERSION = "9";
    const DOCKLIGHT_APPLICATION_RESOURCE =
        "docklight6";
    const DOCKLIGHT_MAIN_ROLE =
        "docklight6-dock";
    const DOCKLIGHT_RESOURCE_PREFIX =
        "docklight6-";
    const DOCKLIGHT_ROLE_PREFIX =
        "docklight6-";
    // KWin's script API exposes Window.layer as a number but does not expose
    // the Layer enum constants. The main layer-shell surface uses DockLayer
    // (3); reveal, tooltip, and preview surfaces use the overlay layer and
    // must never become the dock geometry source.
    const KWIN_DOCK_LAYER = 3;

    let connected = false;
    let registering = false;
    let pendingCommandWaits = 0;
    let revision = 0;
    let commandTransactionDepth = 0;
    let stackingOrderDirty = false;
    let lastPublishedStackingOrder = "";
    let dockPlacementRequestPending = false;

    const trackedWindows = {};
    const workAreaWindows = {};
    const baseDockWorkAreas = {};
    let dockSurface = null;
    let lastDockWorkAreaGeometry = null;
    let lastPublishedDockPointerInside = null;

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
            !isDocklightWindow(window) &&
            windowId(window));
    }

    function isDocklightWindow(window) {
        const resourceName = String(
            window && window.resourceName || "");
        const windowRole = String(
            window && window.windowRole || "");

        return Boolean(
            window &&
            (resourceName ===
                DOCKLIGHT_APPLICATION_RESOURCE ||
             resourceName.startsWith(
                 DOCKLIGHT_RESOURCE_PREFIX) ||
             windowRole.startsWith(
                 DOCKLIGHT_ROLE_PREFIX)));
    }

    function isDocklightDockSurface(window) {
        const resourceName = String(
            window && window.resourceName || "");
        const windowRole = String(
            window && window.windowRole || "");

        // X11/XWayland surfaces share the application's resource name, so
        // WM_WINDOW_ROLE is the semantic discriminator there. KWin exposes
        // the same application resource for native layer surfaces; their
        // configured layer distinguishes the main dock from its overlays.
        const hasMainDockIdentity =
            window && window.x11Client === true
                ? windowRole ===
                    DOCKLIGHT_MAIN_ROLE
                : resourceName ===
                    DOCKLIGHT_APPLICATION_RESOURCE &&
                  Number(window.layer) ===
                    KWIN_DOCK_LAYER;

        return Boolean(
            window &&
            !window.deleted &&
            !window.popupWindow &&
            !window.tooltip &&
            !window.outline &&
            !window.desktopWindow &&
            window.skipTaskbar &&
            hasMainDockIdentity);
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
                isDocklightDockSurface(
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

    function decodedList(encoded) {
        if (!encoded)
            return [];

        return encoded
            .split(",")
            .map(
                value =>
                    decodeURIComponent(value));
    }

    function desktopId(desktop) {
        if (!desktop)
            return "";

        if (typeof desktop.id === "function")
            return desktop.id();

        return desktop.id || "";
    }

    function desktopNumber(desktop) {
        if (!desktop)
            return null;

        const number =
            Number(
                desktop.x11DesktopNumber);

        if (!isFinite(number) ||
            number < 1)
            return null;

        return Math.round(number);
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

    function applicationId(window) {
        if (!window)
            return "";

        return String(
            window.desktopFileName ||
            window.resourceClass ||
            window.resourceName ||
            "");
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

    function isApplicationAuxiliary(window) {
        if (!window || !window.skipTaskbar || !window.keepAbove)
            return false;

        const candidateApplicationId = applicationId(window);
        const candidatePid = Number(window.pid) || 0;

        return (workspace.stackingOrder || []).some(candidate => {
            if (candidate === window ||
                !isTrackable(candidate) ||
                candidate.skipTaskbar)
                return false;

            const sameApplication = candidateApplicationId &&
                applicationId(candidate) === candidateApplicationId;
            const sameProcess = candidatePid > 0 &&
                Number(candidate.pid) === candidatePid;
            return sameApplication || sameProcess;
        });
    }

    function nextRevision() {
        ++revision;
        return String(revision);
    }

    function handlePublishReply(accepted) {
        if (accepted === true)
            return;

        connected = false;
        pendingCommandWaits = 0;
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
                applicationId(window),
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
                    desktopId),
                encodedList(
                    window.desktops,
                    desktopNumber),
                booleanText(
                    windowOnDesktop(
                        window,
                        workspace.currentDesktop)),
                booleanText(
                    isApplicationAuxiliary(window))
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

    function publishActiveWindow(window) {
        // Clicking Docklight must not erase the previously active
        // application. Otherwise the following icon release always looks
        // like an inactive group and repeatedly raises it instead of
        // toggling it hidden.
        if (isDocklightWindow(window) ||
            isDocklightWindow(
                workspace.activeWindow)) {
            return;
        }

        const identifier =
            activeWindowId();

        // KWin can briefly report no active window while a layer-shell dock
        // handles a pointer click. Clearing the previous application here
        // makes every dock click look like an activation request. Genuine
        // focus changes publish the new window ID, while minimized and
        // removed windows have their own state notifications.
        if (!identifier)
            return;

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
            identifier,
            handlePublishReply);
    }

    function publishStackingOrder() {
        if (!connected) {
            registerIntegration();
            return;
        }

        if (commandTransactionDepth > 0) {
            stackingOrderDirty = true;
            return;
        }

        const order = stackingOrder();

        if (order === lastPublishedStackingOrder)
            return;

        lastPublishedStackingOrder = order;

        callDBus(
            SERVICE_NAME,
            OBJECT_PATH,
            INTERFACE_NAME,
            "PublishStackingOrder",
            nextRevision(),
            order,
            handlePublishReply);
    }

    function publishCurrentDesktop() {
        if (!connected) {
            registerIntegration();
            return;
        }

        callDBus(
            SERVICE_NAME,
            OBJECT_PATH,
            INTERFACE_NAME,
            "PublishCurrentDesktop",
            nextRevision(),
            desktopId(
                workspace.currentDesktop),
            desktopNumber(
                workspace.currentDesktop) || 0,
            handlePublishReply);
    }

    function publishDockSurfaceGeometry() {
        if (!connected) {
            registerIntegration();
            return;
        }

        const geometry =
            dockSurface &&
            isDocklightDockSurface(dockSurface)
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

        applyDockPlacementGeometry();
    }

    function applyDockPlacementGeometry() {
        const surface = dockSurface;

        // KWin can constrain an XWayland dock against the full frame of an
        // existing Plasma panel after GTK has moved it to the reserved work
        // area edge. Native layer surfaces remain compositor-positioned.
        if (!connected ||
            dockPlacementRequestPending ||
            !isDocklightDockSurface(surface) ||
            surface.x11Client !== true) {
            return;
        }

        dockPlacementRequestPending = true;

        callDBus(
            SERVICE_NAME,
            OBJECT_PATH,
            INTERFACE_NAME,
            "GetDockPlacementGeometry",
            function (
                valid,
                x,
                y,
                width,
                height) {
                dockPlacementRequestPending = false;

                if (!connected ||
                    dockSurface !== surface ||
                    valid !== true ||
                    Number(width) <= 0 ||
                    Number(height) <= 0) {
                    return;
                }

                const current =
                    surface.frameGeometry || {};
                const target = {
                    x: Number(x),
                    y: Number(y),
                    width: Number(width),
                    height: Number(height)
                };

                if (Number(current.x) === target.x &&
                    Number(current.y) === target.y &&
                    Number(current.width) ===
                        target.width &&
                    Number(current.height) ===
                        target.height) {
                    return;
                }

                surface.frameGeometry = target;
            });
    }

    function adjustedDockWorkArea(
        workArea,
        surface) {
        if (!surface ||
            !surface.output ||
            !surface.output.geometry) {
            return workArea;
        }

        const output = surface.output.geometry;
        const surfaceGeometry =
            surface.frameGeometry || {};
        const outputLeft = Number(output.x || 0);
        const outputTop = Number(output.y || 0);
        const outputRight =
            outputLeft + Number(output.width || 0);
        const outputBottom =
            outputTop + Number(output.height || 0);
        let areaLeft = Number(workArea.x || 0);
        let areaTop = Number(workArea.y || 0);
        let areaRight =
            areaLeft + Number(workArea.width || 0);
        let areaBottom =
            areaTop + Number(workArea.height || 0);
        const leftInset = areaLeft - outputLeft;
        const topInset = areaTop - outputTop;
        const rightInset = outputRight - areaRight;
        const bottomInset = outputBottom - areaBottom;
        const surfaceX = Number(
            surfaceGeometry.x || 0);
        const surfaceY = Number(
            surfaceGeometry.y || 0);
        const surfaceWidth = Number(
            surfaceGeometry.width || 0);
        const surfaceHeight = Number(
            surfaceGeometry.height || 0);
        const horizontalSurface =
            surfaceWidth >= surfaceHeight;
        let ownTopReservation = false;
        let ownBottomReservation = false;
        let ownLeftReservation = false;
        let ownRightReservation = false;

        // MaximizeArea includes all current struts, including Docklight's
        // when autohide is disabled. Recognize that contribution from the
        // surface's far edge and remove it before applying other dock windows;
        // otherwise each relayout feeds our own margin back into the next one.
        if (horizontalSurface) {
            const distanceTop =
                Math.abs(surfaceY - outputTop);
            const distanceBottom =
                Math.abs(
                    outputBottom -
                    (surfaceY + surfaceHeight));

            if (distanceTop <= distanceBottom) {
                const ownInset =
                    surfaceY + surfaceHeight -
                    outputTop;
                ownTopReservation =
                    ownInset > 0 &&
                    Math.abs(topInset - ownInset) <= 1;
                if (ownTopReservation)
                    areaTop = outputTop;
            } else {
                const ownInset =
                    outputBottom - surfaceY;
                ownBottomReservation =
                    ownInset > 0 &&
                    Math.abs(bottomInset - ownInset) <= 1;
                if (ownBottomReservation)
                    areaBottom = outputBottom;
            }
        } else {
            const distanceLeft =
                Math.abs(surfaceX - outputLeft);
            const distanceRight =
                Math.abs(
                    outputRight -
                    (surfaceX + surfaceWidth));

            if (distanceLeft <= distanceRight) {
                const ownInset =
                    surfaceX + surfaceWidth -
                    outputLeft;
                ownLeftReservation =
                    ownInset > 0 &&
                    Math.abs(leftInset - ownInset) <= 1;
                if (ownLeftReservation)
                    areaLeft = outputLeft;
            } else {
                const ownInset =
                    outputRight - surfaceX;
                ownRightReservation =
                    ownInset > 0 &&
                    Math.abs(rightInset - ownInset) <= 1;
                if (ownRightReservation)
                    areaRight = outputRight;
            }
        }

        const outputKey = [
            outputLeft,
            outputTop,
            outputRight,
            outputBottom
        ].join(",");
        const hasOwnReservation =
            ownTopReservation ||
            ownBottomReservation ||
            ownLeftReservation ||
            ownRightReservation;
        const cached =
            baseDockWorkAreas[outputKey];

        // Preserve KWin's raw MaximizeArea before Docklight publishes its
        // own exclusive zone. Once that zone appears, update every unaffected
        // edge but retain the cached value at Docklight's edge. This keeps the
        // Plasma panel's actual reserved content area (not its larger floating
        // frame or Docklight's own reservation) as the placement contract.
        if (!hasOwnReservation) {
            const base = {
                x: Number(workArea.x || 0),
                y: Number(workArea.y || 0),
                width: Number(workArea.width || 0),
                height: Number(workArea.height || 0)
            };

            baseDockWorkAreas[outputKey] = base;
            return base;
        }

        if (cached) {
            const cachedRight =
                Number(cached.x || 0) +
                Number(cached.width || 0);
            const cachedBottom =
                Number(cached.y || 0) +
                Number(cached.height || 0);

            areaLeft = ownLeftReservation
                ? Number(cached.x || 0)
                : Number(workArea.x || 0);
            areaTop = ownTopReservation
                ? Number(cached.y || 0)
                : Number(workArea.y || 0);
            areaRight = ownRightReservation
                ? cachedRight
                : Number(workArea.x || 0) +
                    Number(workArea.width || 0);
            areaBottom = ownBottomReservation
                ? cachedBottom
                : Number(workArea.y || 0) +
                    Number(workArea.height || 0);

            const base = {
                x: areaLeft,
                y: areaTop,
                width: Math.max(
                    1,
                    areaRight - areaLeft),
                height: Math.max(
                    1,
                    areaBottom - areaTop)
            };

            baseDockWorkAreas[outputKey] = base;
            return base;
        }

        function rangesOverlap(
            firstStart,
            firstEnd,
            secondStart,
            secondEnd) {
            return firstStart < secondEnd &&
                secondStart < firstEnd;
        }

        for (const window of
             workspace.stackingOrder || []) {
            if (!window ||
                !window.dock ||
                isDocklightWindow(window)) {
                continue;
            }

            const frame = window.frameGeometry || {};
            const x = Number(frame.x || 0);
            const y = Number(frame.y || 0);
            const width = Number(frame.width || 0);
            const height = Number(frame.height || 0);
            const right = x + width;
            const bottom = y + height;

            if (width <= 0 || height <= 0)
                continue;

            // Plasma can expose a shell-side companion for a layer surface.
            // It has exactly Docklight's bounds and must not be mistaken for
            // a desktop panel while refining KWin's reported strut area.
            if (x === Number(
                    surfaceGeometry.x || 0) &&
                y === Number(
                    surfaceGeometry.y || 0) &&
                width === Number(
                    surfaceGeometry.width || 0) &&
                height === Number(
                    surfaceGeometry.height || 0)) {
                continue;
            }

            if ((topInset > 0 ||
                 ownTopReservation) &&
                y <= outputTop &&
                bottom > outputTop &&
                rangesOverlap(
                    x,
                    right,
                    outputLeft,
                    outputRight)) {
                const outerGap = Math.max(
                    0,
                    ownTopReservation
                        ? 0
                        : (height - topInset) / 2);
                areaTop = Math.max(
                    areaTop,
                    Math.min(
                        outputBottom,
                        Math.round(
                            bottom - outerGap)));
            }

            if ((bottomInset > 0 ||
                 ownBottomReservation) &&
                bottom >= outputBottom &&
                y < outputBottom &&
                rangesOverlap(
                    x,
                    right,
                    outputLeft,
                    outputRight)) {
                const outerGap = Math.max(
                    0,
                    ownBottomReservation
                        ? 0
                        : (height - bottomInset) / 2);
                areaBottom = Math.min(
                    areaBottom,
                    Math.max(
                        outputTop,
                        Math.round(
                            y + outerGap)));
            }

            if ((leftInset > 0 ||
                 ownLeftReservation) &&
                x <= outputLeft &&
                right > outputLeft &&
                rangesOverlap(
                    y,
                    bottom,
                    outputTop,
                    outputBottom)) {
                const outerGap = Math.max(
                    0,
                    ownLeftReservation
                        ? 0
                        : (width - leftInset) / 2);
                areaLeft = Math.max(
                    areaLeft,
                    Math.min(
                        outputRight,
                        Math.round(
                            right - outerGap)));
            }

            if ((rightInset > 0 ||
                 ownRightReservation) &&
                right >= outputRight &&
                x < outputRight &&
                rangesOverlap(
                    y,
                    bottom,
                    outputTop,
                    outputBottom)) {
                const outerGap = Math.max(
                    0,
                    ownRightReservation
                        ? 0
                        : (width - rightInset) / 2);
                areaRight = Math.min(
                    areaRight,
                    Math.max(
                        outputLeft,
                        Math.round(
                            x + outerGap)));
            }
        }

        return {
            x: areaLeft,
            y: areaTop,
            width: Math.max(
                1,
                areaRight - areaLeft),
            height: Math.max(
                1,
                areaBottom - areaTop)
        };
    }

    function publishDockWorkAreaGeometry() {
        if (!connected) {
            registerIntegration();
            return;
        }

        // Native autohide temporarily unmaps the main dock. Preserve its
        // authoritative KDE work area until it remaps; publishing an empty
        // area here would recenter a vertical dock and interrupt the hide.
        let geometry =
            lastDockWorkAreaGeometry || {};

        if (dockSurface &&
            isDocklightDockSurface(dockSurface) &&
            workspace.clientArea &&
            typeof workspace.clientArea ===
                "function") {
            // The Window overload selects this surface's current output and
            // retains Plasma panel struts there. adjustedDockWorkArea removes
            // Docklight's own strut when non-autohide mode has published one.
            // GTK 3 reports the full output as GdkMonitor::workarea on native
            // Plasma Wayland, so this is the authoritative per-output inset.
            geometry =
                adjustedDockWorkArea(
                    workspace.clientArea(
                        KWin.MaximizeArea,
                        dockSurface) || {},
                    dockSurface);

            if (Number(geometry.width) > 0 &&
                Number(geometry.height) > 0) {
                lastDockWorkAreaGeometry = {
                    x: Number(geometry.x || 0),
                    y: Number(geometry.y || 0),
                    width: Number(geometry.width || 0),
                    height: Number(geometry.height || 0)
                };
            }
        }

        callDBus(
            SERVICE_NAME,
            OBJECT_PATH,
            INTERFACE_NAME,
            "PublishDockWorkAreaGeometry",
            nextRevision(),
            Number(geometry.x || 0),
            Number(geometry.y || 0),
            Number(geometry.width || 0),
            Number(geometry.height || 0),
            handlePublishReply);
    }

    function dockPointerIsInside() {
        if (!dockSurface ||
            !isDocklightDockSurface(dockSurface)) {
            return false;
        }

        const position = workspace.cursorPos || {};
        const geometry =
            dockSurface.frameGeometry || {};
        const x = Number(position.x || 0);
        const y = Number(position.y || 0);
        const left = Number(geometry.x || 0);
        const top = Number(geometry.y || 0);
        const width = Number(geometry.width || 0);
        const height = Number(geometry.height || 0);

        return width > 0 &&
            height > 0 &&
            x >= left &&
            y >= top &&
            x < left + width &&
            y < top + height;
    }

    function publishDockPointerInside() {
        if (!connected) {
            registerIntegration();
            return;
        }

        const inside = dockPointerIsInside();
        if (inside ===
            lastPublishedDockPointerInside) {
            return;
        }

        lastPublishedDockPointerInside = inside;
        callDBus(
            SERVICE_NAME,
            OBJECT_PATH,
            INTERFACE_NAME,
            "PublishDockPointerInside",
            inside,
            handlePublishReply);
    }

    function connectWorkAreaWindow(window) {
        if (!window ||
            !window.dock ||
            isDocklightWindow(window)) {
            return;
        }

        const identifier = windowId(window);

        if (!identifier ||
            workAreaWindows[identifier]) {
            return;
        }

        workAreaWindows[identifier] = window;

        const publish = function () {
            publishDockWorkAreaGeometry();
        };

        connectSignal(
            window.frameGeometryChanged,
            publish);
        connectSignal(
            window.outputChanged,
            publish);
        connectSignal(
            window.windowShown,
            publish);
        connectSignal(
            window.windowHidden,
            publish);
    }

    function connectDockSurface(window) {
        if (!isDocklightDockSurface(window))
            return false;

        if (dockSurface === window)
            return true;

        if (isDocklightDockSurface(dockSurface))
            return false;

        dockSurface = window;

        connectSignal(
            window.frameGeometryChanged,
            function () {
                if (dockSurface === window) {
                    publishDockSurfaceGeometry();
                    publishDockWorkAreaGeometry();
                    publishDockPointerInside();
                }
            });

        connectSignal(
            window.outputChanged,
            function () {
                if (dockSurface === window)
                    publishDockWorkAreaGeometry();
            });

        return true;
    }

    function windowOnDesktop(
        window,
        desktop) {
        if (window.onAllDesktops)
            return true;

        const desktops =
            window.desktops || [];

        if (desktops.length === 0)
            return true;

        const currentId =
            desktopId(desktop);

        for (let index = 0;
             index < desktops.length;
             ++index) {
            if (desktops[index] === desktop ||
                (currentId &&
                 desktopId(desktops[index]) ===
                    currentId)) {
                return true;
            }
        }

        return false;
    }

    function windowOnActivity(
        window,
        activity) {
        const activities =
            window.activities || [];

        return activities.length === 0 ||
            activities.indexOf(activity) !== -1;
    }

    function activateWindow(window) {
        const activities =
            window.activities || [];

        if (activities.length > 0 &&
            !windowOnActivity(
                window,
                workspace.currentActivity)) {
            workspace.currentActivity =
                activities[0];
        }

        const desktops =
            window.desktops || [];

        if (desktops.length > 0 &&
            !windowOnDesktop(
                window,
                workspace.currentDesktop)) {
            workspace.currentDesktop =
                desktops[0];
        }

        // KWin 6 exposes activation through the writable activeWindow
        // property.  activateWindow() is not part of Workspace's scripting
        // API and raises a TypeError on current Plasma releases.
        workspace.activeWindow = window;
    }

    function executeCommand(
        command,
        identifier,
        state) {
        if (command === "hide") {
            const identifiers =
                decodedList(identifier);

            for (let index = 0;
                 index < identifiers.length;
                 ++index) {
                const candidate =
                    trackedWindows[
                        identifiers[index]];

                if (candidate &&
                    isTrackable(candidate)) {
                    candidate.minimized =
                        true;
                }
            }

            return;
        }

        if (command === "present") {
            const identifiers =
                decodedList(identifier);
            const windows = [];

            for (let index = 0;
                 index < identifiers.length;
                 ++index) {
                const candidate =
                    trackedWindows[
                        identifiers[index]];

                if (candidate &&
                    isTrackable(candidate)) {
                    windows.push(candidate);
                }
            }

            if (windows.length === 0)
                return;

            for (let index = 0;
                 index < windows.length;
                 ++index) {
                windows[index].minimized =
                    false;
                workspace.raiseWindow(
                    windows[index]);
            }

            activateWindow(
                windows[windows.length - 1]);
            return;
        }

        const window =
            trackedWindows[identifier];

        if (!window ||
            !isTrackable(window))
            return;

        if (command === "activate") {
            activateWindow(window);
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
        const commandWaitCount = 2;

        if (!connected)
            return;

        while (pendingCommandWaits <
               commandWaitCount) {
            ++pendingCommandWaits;

            callDBus(
                SERVICE_NAME,
                OBJECT_PATH,
                INTERFACE_NAME,
                "WaitForCommand",
                function (
                    command,
                    identifier,
                    state) {
                    --pendingCommandWaits;

                    if (!connected)
                        return;

                    // Replenish the consumed request before executing. One
                    // other long poll remains pending at Docklight while
                    // KWin processes this window transaction.
                    waitForCommand();

                    ++commandTransactionDepth;

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
                        --commandTransactionDepth;

                        if (commandTransactionDepth === 0 &&
                            stackingOrderDirty) {
                            stackingOrderDirty = false;
                            publishStackingOrder();
                        }
                    }
                });
        }
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

        // Intellihide needs live frame geometry while windows move and
        // resize. The registry routes geometry-only updates through a light
        // signal, and the dock controller coalesces overlap evaluation on the
        // GTK main loop without rebuilding application items.
        connectSignal(
            window.frameGeometryChanged,
            publish);
        connectSignal(
            window.skipTaskbarChanged,
            publish);
        connectSignal(
            window.keepAboveChanged,
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
            window.resourceClassChanged,
            publish);
        connectSignal(
            window.resourceNameChanged,
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

        lastPublishedStackingOrder =
            stackingOrder();

        publishDockSurfaceGeometry();
        publishDockWorkAreaGeometry();
        publishDockPointerInside();
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
                    pendingCommandWaits = 0;
                    return;
                }

                revision = 0;
                lastPublishedDockPointerInside = null;
                waitForCommand();
                publishSnapshot();
                publishCurrentDesktop();
            });
    }

    function onWindowAdded(window) {
        const isDockSurface =
            connectDockSurface(window);

        connectWorkAreaWindow(window);

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

        const wasWorkAreaWindow =
            Boolean(
                workAreaWindows[identifier]);

        delete workAreaWindows[identifier];

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

        if (wasDockSurface) {
            publishDockSurfaceGeometry();
            publishDockPointerInside();
        }

        if (wasDockSurface ||
            wasWorkAreaWindow) {
            publishDockWorkAreaGeometry();
        }

        publishStackingOrder();
    }

    const initialWindows =
        workspace.stackingOrder || [];

    for (let index = 0;
         index < initialWindows.length;
         ++index) {
        connectDockSurface(
            initialWindows[index]);
        connectWorkAreaWindow(
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
    connectSignal(
        workspace.currentDesktopChanged,
        publishCurrentDesktop);
    connectSignal(
        workspace.cursorPosChanged,
        publishDockPointerInside);
    connectSignal(
        workspace.screenAdded,
        publishDockWorkAreaGeometry);
    connectSignal(
        workspace.screenRemoved,
        publishDockWorkAreaGeometry);

    registerIntegration();
}());
