#!/usr/bin/env bash

set -euo pipefail

readonly SERVICE="org.docklight6.WindowIntegration"
readonly OBJECT_PATH="/org/docklight6/WindowIntegration"
readonly INTERFACE="org.docklight6.WindowIntegration1"

failures=0
warnings=0

pass()
{
    echo "PASS  $*"
}

warn()
{
    echo "WARN  $*"
    warnings=$((warnings + 1))
}

fail()
{
    echo "FAIL  $*"
    failures=$((failures + 1))
}

require_command()
{
    if ! command -v "$1" >/dev/null 2>&1; then
        fail "required diagnostic command is missing: $1"
        return 1
    fi
}

find_dock_window()
{
    wmctrl -lx 2>/dev/null |
        awk 'tolower($3) == "docklight6.docklight6" &&
             index($0, "Docklight 6 Dock") { print $1; exit }'
}

property_contains()
{
    local properties="$1"
    local expected="$2"
    grep -Fq "${expected}" <<<"${properties}"
}

require_command pgrep || true
require_command wmctrl || true
require_command xprop || true
require_command gdbus || true

process_id="$(pgrep -u "$(id -u)" -x docklight6 | head -n 1 || true)"
if [[ -z ${process_id} ]]; then
    fail "Docklight is not running"
else
    pass "Docklight process is running (PID ${process_id})"
fi

window_id="$(find_dock_window || true)"
if [[ -z ${window_id} ]]; then
    fail "Docklight XWayland window was not found"
else
    pass "Docklight is presented through XWayland (${window_id})"

    properties="$(xprop -id "${window_id}" \
        _NET_WM_WINDOW_TYPE \
        _NET_WM_STATE \
        _NET_WM_DESKTOP \
        _NET_WM_STRUT \
        _NET_WM_STRUT_PARTIAL \
        WM_WINDOW_ROLE \
        _NET_WM_PID 2>/dev/null || true)"

    for expected in \
        _NET_WM_WINDOW_TYPE_DOCK \
        _NET_WM_STATE_SKIP_PAGER \
        _NET_WM_STATE_SKIP_TASKBAR \
        _NET_WM_STATE_ABOVE \
        _NET_WM_STATE_STICKY \
        'WM_WINDOW_ROLE(STRING) = "docklight6-dock"'
    do
        if property_contains "${properties}" "${expected}"; then
            pass "X11 property present: ${expected}"
        else
            fail "X11 property missing: ${expected}"
        fi
    done

    if property_contains \
        "${properties}" \
        '_NET_WM_DESKTOP(CARDINAL) = 4294967295'
    then
        pass "Docklight is visible on all workspaces"
    else
        fail "Docklight is not assigned to all workspaces"
    fi

    if property_contains \
        "${properties}" \
        '_NET_WM_STRUT_PARTIAL(CARDINAL)'; then
        pass "X11 work-area reservation is published"
    else
        warn "no X11 strut is published (expected while autohide is enabled)"
    fi
fi

geometry="$(gdbus call --session \
    --dest "${SERVICE}" \
    --object-path "${OBJECT_PATH}" \
    --method "${INTERFACE}.GetDockSurfaceGeometry" \
    2>/dev/null || true)"

if [[ ${geometry} == \(true,* ]]; then
    pass "GNOME integration reports dock geometry: ${geometry}"
else
    fail "GNOME integration does not report valid dock geometry"
fi

hidden="$(gdbus call --session \
    --dest "${SERVICE}" \
    --object-path "${OBJECT_PATH}" \
    --method "${INTERFACE}.GetDockHidden" \
    2>/dev/null || true)"

if [[ ${hidden} == '(true,)' || ${hidden} == '(false,)' ]]; then
    pass "GNOME integration reports dock visibility: ${hidden}"
else
    fail "GNOME integration does not report dock visibility"
fi

echo
echo "Presentation validation: ${failures} failure(s), ${warnings} warning(s)"
((failures == 0))
