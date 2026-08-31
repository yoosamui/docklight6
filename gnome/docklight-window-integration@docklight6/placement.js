// Pure GNOME Shell placement helpers. Keeping these calculations independent
// of Meta.Window makes multi-monitor edge behavior testable without Mutter.

export function inferDockEdge(monitor, rect) {
    let distances = [
        ['top', Math.abs(rect.y - monitor.y)],
        ['bottom', Math.abs(
            monitor.y + monitor.height - rect.y - rect.height)],
        ['left', Math.abs(rect.x - monitor.x)],
        ['right', Math.abs(
            monitor.x + monitor.width - rect.x - rect.width)],
    ];

    // A fill-aligned dock can touch a corner, making two edge distances
    // identical. Prefer the axis implied by its shape so a full-height LEFT
    // dock is not mistaken for TOP (and animated vertically).
    if (rect.height > rect.width)
        distances = distances.filter(([edge]) =>
            edge === 'left' || edge === 'right');
    else if (rect.width > rect.height)
        distances = distances.filter(([edge]) =>
            edge === 'top' || edge === 'bottom');

    distances.sort((left, right) => left[1] - right[1]);
    return distances[0][0];
}

export function calculateDockHideOffset(placement) {
    if (placement.edge === 'top')
        return {x: 0, y: -placement.height};
    if (placement.edge === 'bottom')
        return {x: 0, y: placement.height};
    if (placement.edge === 'left')
        return {x: -placement.width, y: 0};
    return {x: placement.width, y: 0};
}

// Experimental RIGHT-edge policy: collapse only when the dock's complete
// outward slide corridor overlaps another monitor. Other edges deliberately
// retain their existing animation while this behavior is evaluated.
export function rightHideCorridorIntersectsMonitor(
    placement, monitorIndex, monitors) {
    if (placement?.edge !== 'right' || !Array.isArray(monitors))
        return false;

    const corridor = {
        x: placement.x + placement.width,
        y: placement.y,
        width: placement.width,
        height: placement.height,
    };

    return monitors.some((monitor, index) => {
        if (!monitor || index === monitorIndex)
            return false;

        return corridor.x < monitor.x + monitor.width &&
            corridor.x + corridor.width > monitor.x &&
            corridor.y < monitor.y + monitor.height &&
            corridor.y + corridor.height > monitor.y;
    });
}

export function parseAuxiliaryPosition(title) {
    const match = String(title || '').match(
        /^Docklight 6 (Tooltip|Preview|Reveal)@(-?\d+),(-?\d+)$/);
    if (!match)
        return null;

    return {
        type: match[1].toLowerCase(),
        x: Number.parseInt(match[2], 10),
        y: Number.parseInt(match[3], 10),
    };
}

function clamp(value, minimum, maximum) {
    return Math.min(Math.max(value, minimum), Math.max(minimum, maximum));
}

export function clampAuxiliaryToWorkArea(
    position,
    size,
    workArea,
    edgeMargin = 8) {
    if (!workArea || workArea.width <= 0 || workArea.height <= 0)
        return {x: Math.round(position.x), y: Math.round(position.y)};

    const margin = Math.max(0, Math.round(edgeMargin));
    const width = Math.max(1, Math.round(size.width));
    const height = Math.max(1, Math.round(size.height));
    const minimumX = Math.round(workArea.x) + margin;
    const minimumY = Math.round(workArea.y) + margin;
    const maximumX = Math.round(workArea.x + workArea.width) - width - margin;
    const maximumY = Math.round(workArea.y + workArea.height) - height - margin;

    return {
        x: clamp(Math.round(position.x), minimumX, maximumX),
        y: clamp(Math.round(position.y), minimumY, maximumY),
    };
}

export function calculateDockRevealRect(placement, revealSize = 6) {
    const size = Math.max(1, Math.round(revealSize));
    const rect = {
        x: Math.round(placement.x),
        y: Math.round(placement.y),
        width: Math.round(placement.width),
        height: Math.round(placement.height),
    };

    if (placement.edge === 'top')
        rect.height = size;
    else if (placement.edge === 'bottom') {
        rect.y += Math.max(0, rect.height - size);
        rect.height = size;
    } else if (placement.edge === 'left')
        rect.width = size;
    else {
        rect.x += Math.max(0, rect.width - size);
        rect.width = size;
    }

    return rect;
}

export function placeDockInWorkArea(
    monitor,
    workArea,
    rect,
    alignment = 'center',
    configuredEdge = null) {
    const area = workArea || monitor;
    const edge = ['top', 'bottom', 'left', 'right'].includes(configuredEdge)
        ? configuredEdge
        : inferDockEdge(monitor, rect);
    let x = rect.x;
    let y = rect.y;

    const alignMainAxis = (areaStart, areaLength, surfaceLength) => {
        if (alignment === 'start' || alignment === 'fill')
            return areaStart;
        if (alignment === 'end')
            return areaStart + areaLength - surfaceLength;
        return areaStart + (areaLength - surfaceLength) / 2;
    };

    if (edge === 'top' || edge === 'bottom') {
        x = alignMainAxis(area.x, area.width, rect.width);
        y = edge === 'top'
            ? area.y
            : area.y + area.height - rect.height;
    } else {
        x = edge === 'left'
            ? area.x
            : area.x + area.width - rect.width;
        y = alignMainAxis(area.y, area.height, rect.height);
    }

    x = clamp(x, area.x, area.x + area.width - rect.width);
    y = clamp(y, area.y, area.y + area.height - rect.height);

    return {
        x: Math.round(x),
        y: Math.round(y),
        width: Math.round(rect.width),
        height: Math.round(rect.height),
        edge,
    };
}

export function isDockPlacementCommitted(frameRect, placement, actorOffset) {
    const offset = actorOffset || {x: 0, y: 0};
    return Math.round(frameRect.x + offset.x) === Math.round(placement.x) &&
        Math.round(frameRect.y + offset.y) === Math.round(placement.y);
}

export function dockMonitorIndexForRect(
    rect, monitors, primaryIndex = 0) {
    const availableMonitors = Array.isArray(monitors) ? monitors : [];
    const requestedPrimary = Number.isInteger(primaryIndex) && primaryIndex >= 0
        ? primaryIndex
        : 0;
    const fallbackIndex = availableMonitors.length === 0 ||
        availableMonitors[requestedPrimary]
        ? requestedPrimary
        : 0;
    if (!rect || availableMonitors.length === 0)
        return fallbackIndex;

    let bestIndex = fallbackIndex;
    let bestArea = -1;
    for (let index = 0; index < availableMonitors.length; index++) {
        const monitor = availableMonitors[index];
        if (!monitor)
            continue;

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

export function dockPlacementChangesMonitor(
    previous, next, monitors, primaryIndex = 0) {
    if (!previous || !next)
        return false;

    return dockMonitorIndexForRect(
        previous, monitors, primaryIndex) !==
        dockMonitorIndexForRect(next, monitors, primaryIndex);
}

export function isPointerInsideDockInterior(
    placement, pointerX, pointerY, edgeMargin = 0) {
    const insideRect =
        pointerX >= placement.x &&
        pointerX < placement.x + placement.width &&
        pointerY >= placement.y &&
        pointerY < placement.y + placement.height;
    if (!insideRect)
        return false;

    if (placement.edge === 'top')
        return pointerY >= placement.y + edgeMargin;
    if (placement.edge === 'bottom')
        return pointerY < placement.y + placement.height - edgeMargin;
    if (placement.edge === 'left')
        return pointerX >= placement.x + edgeMargin;
    return pointerX < placement.x + placement.width - edgeMargin;
}

export function isSyntheticApplicationId(applicationId) {
    return /^window:\d+$/i.test(String(applicationId || '').trim());
}

export function calculateDockStrut(monitor, dockRect) {
    const edge = ['top', 'bottom', 'left', 'right'].includes(dockRect.edge)
        ? dockRect.edge
        : inferDockEdge(monitor, dockRect);
    let x = monitor.x;
    let y = monitor.y;
    let width = monitor.width;
    let height = monitor.height;
    let actorOffset = {x: 0, y: 0};

    if (edge === 'top') {
        height = Math.max(1, dockRect.y + dockRect.height - monitor.y);
        actorOffset = {x: 0, y: -dockRect.height};
    } else if (edge === 'bottom') {
        y = dockRect.y;
        height = Math.max(1, monitor.y + monitor.height - dockRect.y);
        actorOffset = {x: 0, y: dockRect.height};
    } else if (edge === 'left') {
        width = Math.max(1, dockRect.x + dockRect.width - monitor.x);
        actorOffset = {x: -dockRect.width, y: 0};
    } else {
        x = dockRect.x;
        width = Math.max(1, monitor.x + monitor.width - dockRect.x);
        actorOffset = {x: dockRect.width, y: 0};
    }

    return {x, y, width, height, actorOffset};
}
