#!/usr/bin/env bash

set -euo pipefail

readonly SOURCE_DIRECTORY="$(
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." &&
        pwd
)"
readonly TEST_DIRECTORY="$(mktemp -d "${TMPDIR:-/tmp}/docklight-backend-test.XXXXXX")"
readonly TEST_LOG="${TEST_DIRECTORY}/calls.log"
readonly TEST_BIN="${TEST_DIRECTORY}/bin"

cleanup()
{
    rm -rf -- "${TEST_DIRECTORY}"
}

trap cleanup EXIT

mkdir -p \
    "${TEST_BIN}" \
    "${TEST_DIRECTORY}/gnome" \
    "${TEST_DIRECTORY}/kwin" \
    "${TEST_DIRECTORY}/plasma/geometry-bridge"

cp "${SOURCE_DIRECTORY}/setup_backend.sh" "${TEST_DIRECTORY}/setup_backend.sh"

make_stub()
{
    local path="$1"
    local name="$2"

    cat >"${path}" <<EOF
#!/usr/bin/env bash
echo "${name}" >>"\${SETUP_BACKEND_TEST_LOG}"
EOF
    chmod +x "${path}"
}

make_stub \
    "${TEST_DIRECTORY}/gnome/install-window-integration.sh" \
    gnome
make_stub \
    "${TEST_DIRECTORY}/kwin/install-window-integration.sh" \
    plasma
make_stub \
    "${TEST_DIRECTORY}/kwin/install-minimize-effect.sh" \
    minimize-effect
make_stub \
    "${TEST_DIRECTORY}/plasma/geometry-bridge/install-geometry-bridge.sh" \
    geometry-bridge

cat >"${TEST_BIN}/gnome-extensions" <<'EOF'
#!/usr/bin/env bash
case "${1:-}" in
info)
    if [[ "${2:-}" == "docklight-window-integration@docklight6" ]]; then
        cat <<'INFO'
docklight-window-integration@docklight6
  Version: 11
  Enabled: Yes
  State: ACTIVE
INFO
        exit 0
    fi
    exit 1
    ;;
list)
    if [[ "${2:-}" == "--enabled" ]]; then
        echo docklight-window-integration@docklight6
    fi
    ;;
esac
EOF

cat >"${TEST_BIN}/kpackagetool6" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF

cat >"${TEST_BIN}/kreadconfig6" <<'EOF'
#!/usr/bin/env bash
echo true
EOF

cat >"${TEST_BIN}/qdbus6" <<'EOF'
#!/usr/bin/env bash
exit 0
EOF

chmod +x "${TEST_BIN}"/*

run_setup()
{
    env \
        PATH="${TEST_BIN}:${PATH}" \
        SETUP_BACKEND_TEST_LOG="${TEST_LOG}" \
        XDG_CURRENT_DESKTOP="${1}" \
        XDG_SESSION_TYPE="${2}" \
        "${TEST_DIRECTORY}/setup_backend.sh" "${@:3}"
}

assert_calls()
{
    local expected="$1"
    local actual=""

    if [[ -f "${TEST_LOG}" ]]; then
        actual="$(paste -sd, "${TEST_LOG}")"
    fi

    if [[ "${actual}" != "${expected}" ]]; then
        echo "Expected calls '${expected}', got '${actual}'" >&2
        exit 1
    fi

    : >"${TEST_LOG}"
}

run_setup GNOME wayland auto
assert_calls gnome

run_setup GNOME x11 auto
assert_calls gnome

run_setup GNOME-Flashback:GNOME x11 auto
assert_calls ""

run_setup KDE wayland auto
assert_calls plasma

run_setup XFCE x11 auto
assert_calls ""

run_setup GNOME wayland plasma \
    --with-geometry-bridge \
    --with-minimize-effect
assert_calls plasma,geometry-bridge,minimize-effect

run_setup KDE wayland gnome
assert_calls gnome

status_output="$(run_setup GNOME wayland status)"
grep -Fq "Current session backend: gnome" <<<"${status_output}"
grep -Fq "Extension installed: yes" <<<"${status_output}"
grep -Fq "Extension state: ACTIVE" <<<"${status_output}"
grep -Fq "KWin integration installed: yes" <<<"${status_output}"
grep -Fq "Built into Docklight: yes" <<<"${status_output}"
assert_calls ""

run_setup GNOME wayland --help >/dev/null
assert_calls ""

if run_setup sway wayland auto >/dev/null 2>&1; then
    echo "Unsupported Wayland session unexpectedly selected a backend" >&2
    exit 1
fi

if run_setup GNOME wayland gnome --with-minimize-effect >/dev/null 2>&1; then
    echo "Plasma-only option unexpectedly succeeded for GNOME" >&2
    exit 1
fi
