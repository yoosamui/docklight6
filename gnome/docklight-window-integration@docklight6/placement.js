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
        x: Number.parseInt(match[2], 10),
        y: Number.parseInt(match[3], 10),
    };
}

function clamp(value, minimum, maximum) {
    return Math.min(Math.max(value, minimum), Math.max(minimum, maximum));
}

export function placeDockInWorkArea(monitor, workArea, rect) {
    const area = workArea || monitor;
    const edge = inferDockEdge(monitor, rect);
    let x = rect.x;
    let y = rect.y;

    if (edge === 'top' || edge === 'bottom') {
        x = clamp(rect.x, area.x, area.x + area.width - rect.width);
        y = edge === 'top'
            ? area.y
            : area.y + area.height - rect.height;
    } else {
        x = edge === 'left'
            ? area.x
            : area.x + area.width - rect.width;
        y = clamp(rect.y, area.y, area.y + area.height - rect.height);
    }

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
