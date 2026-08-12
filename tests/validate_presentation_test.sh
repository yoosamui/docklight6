#!/usr/bin/env bash

set -euo pipefail

readonly SOURCE_DIRECTORY="$({
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."
    pwd
})"
readonly TEST_DIRECTORY="$(mktemp -d "${TMPDIR:-/tmp}/docklight-presentation-validation.XXXXXX")"
readonly TEST_BIN="${TEST_DIRECTORY}/bin"

cleanup()
{
    rm -rf -- "${TEST_DIRECTORY}"
}

trap cleanup EXIT
mkdir -p "${TEST_BIN}"

cat >"${TEST_BIN}/pgrep" <<'EOF'
#!/usr/bin/env bash
echo 1234
EOF

cat >"${TEST_BIN}/wmctrl" <<'EOF'
#!/usr/bin/env bash
echo '0x01800003 -1 docklight6.Docklight6 host Docklight 6 Dock'
EOF

cat >"${TEST_BIN}/xprop" <<'EOF'
#!/usr/bin/env bash
cat <<'PROPERTIES'
_NET_WM_WINDOW_TYPE(ATOM) = _NET_WM_WINDOW_TYPE_DOCK
_NET_WM_STATE(ATOM) = _NET_WM_STATE_SKIP_PAGER, _NET_WM_STATE_SKIP_TASKBAR, _NET_WM_STATE_ABOVE, _NET_WM_STATE_STICKY
_NET_WM_DESKTOP(CARDINAL) = 4294967295
_NET_WM_STRUT_PARTIAL(CARDINAL) = 0, 0, 0, 62
WM_WINDOW_ROLE(STRING) = "docklight6-dock"
_NET_WM_PID(CARDINAL) = 1234
PROPERTIES
EOF

cat >"${TEST_BIN}/xwininfo" <<'EOF'
#!/usr/bin/env bash
cat <<'WINDOW_INFO'
  Absolute upper-left X:  590
  Absolute upper-left Y:  1378
  Width: 1380
  Height: 62
WINDOW_INFO
EOF

cat >"${TEST_BIN}/gdbus" <<'EOF'
#!/usr/bin/env bash
case "$*" in
*GetDockSurfaceGeometry*) echo '(true, 590, 1378, 1380, 62)' ;;
*GetDockHidden*) echo '(true,)' ;;
*) exit 1 ;;
esac
EOF

chmod +x "${TEST_BIN}"/*

output="$(PATH="${TEST_BIN}:${PATH}" \
    "${SOURCE_DIRECTORY}/validate_presentation.sh")"

grep -Fq 'PASS  Docklight is presented through XWayland' <<<"${output}"
grep -Fq 'PASS  GNOME integration reports dock geometry' <<<"${output}"
grep -Fq 'PASS  GNOME and X11 dock geometry agree' <<<"${output}"
grep -Fq 'Presentation validation: 0 failure(s), 0 warning(s)' <<<"${output}"
