#!/usr/bin/env bash

set -euo pipefail

readonly CONFIGURATION_ROOT="${XDG_CONFIG_HOME:-${HOME}/.config}/docklight6"
readonly CONFIGURATION_PATH="${CONFIGURATION_ROOT}/presentation.conf"

usage()
{
    cat <<'EOF'
Usage: ./setup_presentation.sh <native|xwayland|auto|status>

Select how Docklight's GTK windows are presented. This is independent from
the GNOME, Hyprland, Plasma, or X11 window-integration backend.

  native     Use the desktop session's native GTK presentation
  xwayland   Use X11 dock windows through XWayland in a Wayland session
  auto       Use XWayland on GNOME/Hyprland Wayland; native elsewhere
  status     Report configuration and current-session availability

The setting is stored per user and does not modify Docklight's desktop entry.
Restart Docklight after changing it.
EOF
}

fail()
{
    echo "setup_presentation.sh: $*" >&2
    exit 1
}

configured_modes()
{
    local modes=""

    if [[ -f ${CONFIGURATION_PATH} ]]; then
        modes="$(sed -n \
            's/^[[:space:]]*mode[[:space:]]*=[[:space:]]*\([^[:space:]#]*\).*$/\1/p' \
            "${CONFIGURATION_PATH}" | head -n 1)"
        modes="${modes,,}"
    fi

    local mode=""
    IFS=',' read -r -a configured <<<"${modes}"
    for mode in "${configured[@]}"; do
        [[ ${mode} == auto ||
           ${mode} == native ||
           ${mode} == xwayland ]] || {
            echo auto
            return
        }
    done

    [[ -n ${modes} ]] && echo "${modes}" || echo auto
}

automatic_mode()
{
    local session="${XDG_SESSION_TYPE:-}"
    local desktop="${XDG_CURRENT_DESKTOP:-${XDG_SESSION_DESKTOP:-}}"
    session="${session,,}"
    desktop="${desktop,,}"

    if { [[ ${session} == wayland ]] ||
         { [[ -z ${session} ]] && [[ -n ${WAYLAND_DISPLAY:-} ]]; }; } &&
       { [[ ${desktop} == *gnome* ]] ||
         [[ ${desktop} == *hyprland* ]]; } &&
       [[ -n ${DISPLAY:-} ]]
    then
        echo xwayland
    else
        echo native
    fi
}

resolved_mode()
{
    local mode="$1"

    local session="${XDG_SESSION_TYPE:-}"
    session="${session,,}"

    local configured_mode=""
    IFS=',' read -r -a configured <<<"${mode}"
    for configured_mode in "${configured[@]}"; do
        if [[ ${configured_mode} == auto ]]; then
            automatic_mode
            return
        elif [[ ${configured_mode} == native ]] ||
             { [[ ${configured_mode} == xwayland ]] &&
               [[ -n ${DISPLAY:-} ]] &&
               { [[ ${session} == wayland ]] ||
                 [[ -n ${WAYLAND_DISPLAY:-} ]]; }; }
        then
            echo "${configured_mode}"
            return
        fi
    done

    echo "${mode%%,*}"
}

write_mode()
{
    local mode="$1"
    local temporary_path=""

    mkdir -p -- "${CONFIGURATION_ROOT}"
    temporary_path="$(mktemp \
        "${CONFIGURATION_ROOT}/presentation.conf.XXXXXX")"
    trap 'rm -f -- "${temporary_path}"' EXIT

    printf '# Docklight presentation mode\nmode=%s\n' \
        "${mode}" >"${temporary_path}"
    chmod 600 "${temporary_path}"
    mv -- "${temporary_path}" "${CONFIGURATION_PATH}"
    trap - EXIT
}

session_backend()
{
    local desktop="${XDG_CURRENT_DESKTOP:-unknown}"
    local session="${XDG_SESSION_TYPE:-unknown}"
    echo "${desktop} ${session}"
}

running_mode()
{
    local process_id=""
    process_id="$(pgrep -u "$(id -u)" -x docklight6 | head -n 1 || true)"

    if [[ -z ${process_id} ]]; then
        echo "not running"
        return
    fi

    # /proc/PID/environ can retain the process's startup copy after g_setenv
    # changes GDK_BACKEND. Inspect the actual display-server window instead.
    if command -v wmctrl >/dev/null 2>&1 &&
        wmctrl -lx 2>/dev/null |
            awk 'tolower($3) == "docklight6.docklight6" &&
                 index($0, "Docklight 6 Dock") { found = 1 }
                 END { exit !found }'
    then
        echo xwayland
    else
        echo native
    fi
}

print_status()
{
    local mode=""
    local resolved=""
    mode="$(configured_modes)"
    resolved="$(resolved_mode "${mode}")"

    echo "Docklight presentation"
    echo "  Config path: ${CONFIGURATION_PATH}"
    echo "  Configured mode: ${mode}"
    echo "  Resolved mode: ${resolved}"
    echo "  Current session: $(session_backend)"
    echo "  Wayland display available: $(
        [[ -n ${WAYLAND_DISPLAY:-} ]] && echo yes || echo no
    )"
    echo "  XWayland display available: $(
        [[ -n ${DISPLAY:-} ]] && echo yes || echo no
    )"
    echo "  Running mode: $(running_mode)"

    if [[ ${resolved} == native ]] ||
        { [[ ${resolved} == xwayland ]] &&
          [[ -n ${DISPLAY:-} ]] &&
          { [[ ${XDG_SESSION_TYPE:-} == wayland ]] ||
            [[ -n ${WAYLAND_DISPLAY:-} ]]; }; }
    then
        echo "  Configuration usable now: yes"
    else
        echo "  Configuration usable now: no"
    fi
}

if ((EUID == 0)); then
    fail "run this command as the desktop user, without sudo"
fi

case "${1:-}" in
native)
    write_mode native
    echo "Docklight presentation configured: native"
    echo "Restart Docklight to apply the change."
    ;;
xwayland)
    [[ ${XDG_SESSION_TYPE:-} == wayland || -n ${WAYLAND_DISPLAY:-} ]] ||
        fail "xwayland requires a Wayland session"
    [[ -n ${DISPLAY:-} ]] ||
        fail "xwayland requires DISPLAY from XWayland"
    write_mode xwayland
    echo "Docklight presentation configured: xwayland"
    echo "Window integration remains: $(session_backend)"
    echo "Restart Docklight to apply the change."
    ;;
auto)
    write_mode auto
    echo "Docklight presentation configured: auto"
    echo "Restart Docklight to apply the change."
    ;;
status)
    print_status
    ;;
-h|--help|help)
    usage
    ;;
*)
    usage >&2
    exit 2
    ;;
esac
