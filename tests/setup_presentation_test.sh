#!/usr/bin/env bash

set -euo pipefail

readonly SOURCE_DIRECTORY="$({
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
    pwd
})"
readonly TEST_DIRECTORY="$(mktemp -d "${TMPDIR:-/tmp}/docklight-presentation-setup.XXXXXX")"

cleanup()
{
    rm -rf -- "${TEST_DIRECTORY}"
}

trap cleanup EXIT

run_setup()
{
    env \
        XDG_CONFIG_HOME="${TEST_DIRECTORY}/config" \
        XDG_SESSION_TYPE=wayland \
        WAYLAND_DISPLAY=wayland-test \
        DISPLAY=:99 \
        XDG_CURRENT_DESKTOP=GNOME \
        "${SOURCE_DIRECTORY}/setup_presentation.sh" "$@"
}

run_setup xwayland >/dev/null
grep -Fqx 'mode=xwayland' \
    "${TEST_DIRECTORY}/config/docklight6/presentation.conf"

status="$(run_setup status)"
grep -Fq 'Configured mode: xwayland' <<<"${status}"
grep -Fq 'Current session: GNOME wayland' <<<"${status}"
grep -Fq 'XWayland display available: yes' <<<"${status}"
grep -Fq 'Configuration usable now: yes' <<<"${status}"

run_setup native >/dev/null
grep -Fqx 'mode=native' \
    "${TEST_DIRECTORY}/config/docklight6/presentation.conf"

if env \
    XDG_CONFIG_HOME="${TEST_DIRECTORY}/invalid" \
    XDG_SESSION_TYPE=wayland \
    WAYLAND_DISPLAY=wayland-test \
    DISPLAY= \
    "${SOURCE_DIRECTORY}/setup_presentation.sh" xwayland \
    >/dev/null 2>&1
then
    echo "Persistent XWayland mode accepted a missing DISPLAY" >&2
    exit 1
fi
