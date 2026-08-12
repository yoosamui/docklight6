#!/usr/bin/env bash

set -euo pipefail

readonly SCRIPT_DIRECTORY="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

fail()
{
    echo "Docklight XWayland prototype: $*" >&2
    exit 1
}

[[ ${XDG_SESSION_TYPE:-} == wayland || -n ${WAYLAND_DISPLAY:-} ]] ||
    fail "this launcher must be used from a Wayland session"

[[ -n ${DISPLAY:-} ]] ||
    fail "DISPLAY is missing; XWayland is not available in this session"

if [[ ${DOCKLIGHT_PROTOTYPE_SKIP_RUNNING_CHECK:-0} != 1 ]] &&
    pgrep -u "$(id -u)" -x docklight6 >/dev/null 2>&1
then
    fail "Docklight is already running; close it before starting the prototype"
fi

if [[ -n ${DOCKLIGHT_PROTOTYPE_BINARY:-} ]]; then
    binary=${DOCKLIGHT_PROTOTYPE_BINARY}
elif [[ -x ${SCRIPT_DIRECTORY}/build-debug/src/docklight6 ]]; then
    binary=${SCRIPT_DIRECTORY}/build-debug/src/docklight6
elif [[ -x ${SCRIPT_DIRECTORY}/build/src/docklight6 ]]; then
    binary=${SCRIPT_DIRECTORY}/build/src/docklight6
elif [[ -x /usr/local/bin/docklight6 ]]; then
    binary=/usr/local/bin/docklight6
else
    fail "no Docklight binary found; build the project first"
fi

echo "Docklight presentation: X11/XWayland prototype"
echo "Docklight window integration: ${XDG_CURRENT_DESKTOP:-unknown} Wayland"
echo "Binary: ${binary}"
echo "This changes only this process; the normal launcher remains unchanged."

exec "${binary}" --presentation=xwayland "$@"