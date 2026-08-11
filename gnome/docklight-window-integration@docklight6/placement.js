// Pure GNOME Shell placement helpers. Keeping these calculations independent
// of Meta.Window makes multi-monitor edge behavior testable without Mutter.

export function inferDockEdge(monitor, rect) {
    const distances = [
        ['top', Math.abs(rect.y - monitor.y)],
        ['bottom', Math.abs(
            monitor.y + monitor.height - rect.y - rect.height)],
        ['left', Math.abs(rect.x - monitor.x)],
        ['right', Math.abs(
            monitor.x + monitor.width - rect.x - rect.width)],
    ];
    distances.sort((left, right) => left[1] - right[1]);
    return distances[0][0];
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
    alignment = 'center') {
    const area = workArea || monitor;
    const edge = inferDockEdge(monitor, rect);
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

export function isPointerInsideDockInterior(
    placement, pointerX, pointerY, edgeMargin = 4) {
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

export function calculateDockStrut(monitor, dockRect) {
    const edge = inferDockEdge(monitor, dockRect);
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
