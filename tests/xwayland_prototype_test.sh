#!/usr/bin/env bash

set -euo pipefail

readonly SOURCE_DIRECTORY="$({
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
    pwd
})"
readonly TEST_DIRECTORY="$(mktemp -d "${TMPDIR:-/tmp}/docklight-xwayland-test.XXXXXX")"
readonly TEST_BINARY="${TEST_DIRECTORY}/docklight6"
readonly TEST_LOG="${TEST_DIRECTORY}/arguments"

cleanup()
{
    rm -rf -- "${TEST_DIRECTORY}"
}

trap cleanup EXIT

cat >"${TEST_BINARY}" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$@" >"${DOCKLIGHT_PROTOTYPE_TEST_LOG}"
printf '%s\n' "${XDG_SESSION_TYPE:-}" "${WAYLAND_DISPLAY:-}" \
    "${DISPLAY:-}" >>"${DOCKLIGHT_PROTOTYPE_TEST_LOG}"
EOF
chmod +x "${TEST_BINARY}"

env \
    XDG_SESSION_TYPE=wayland \
    WAYLAND_DISPLAY=wayland-test \
    DISPLAY=:99 \
    XDG_CURRENT_DESKTOP=GNOME \
    DOCKLIGHT_PROTOTYPE_BINARY="${TEST_BINARY}" \
    DOCKLIGHT_PROTOTYPE_SKIP_RUNNING_CHECK=1 \
    DOCKLIGHT_PROTOTYPE_TEST_LOG="${TEST_LOG}" \
    "${SOURCE_DIRECTORY}/run_xwayland_prototype.sh" --list-monitors >/dev/null

mapfile -t result <"${TEST_LOG}"
[[ ${result[0]} == --presentation=xwayland ]]
[[ ${result[1]} == --list-monitors ]]
[[ ${result[2]} == wayland ]]
[[ ${result[3]} == wayland-test ]]
[[ ${result[4]} == :99 ]]

if env -u XDG_SESSION_TYPE -u WAYLAND_DISPLAY \
    DISPLAY=:99 \
    "${SOURCE_DIRECTORY}/run_xwayland_prototype.sh" >/dev/null 2>&1
then
    echo "Prototype accepted a non-Wayland session" >&2
    exit 1
fi

if env XDG_SESSION_TYPE=wayland WAYLAND_DISPLAY=wayland-test DISPLAY= \
    "${SOURCE_DIRECTORY}/run_xwayland_prototype.sh" >/dev/null 2>&1
then
    echo "Prototype accepted a session without XWayland DISPLAY" >&2
    exit 1
fi

# Gtk::Application forwards a second invocation to the primary process as an
# activation. Keep that activation connected to Docklight's reveal path so a
# hidden dock does not make a successful second launch appear to do nothing.
grep -Fq 'app->signal_activate().connect(' \
    "${SOURCE_DIRECTORY}/src/main.cpp"
grep -Fq 'window.request_reveal();' \
    "${SOURCE_DIRECTORY}/src/main.cpp"
