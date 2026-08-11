#!/usr/bin/env bash

set -euo pipefail

readonly SCRIPT_DIRECTORY="$(
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &&
        pwd
)"

usage()
{
    cat <<'EOF'
Usage: ./setup_backend.sh <auto|gnome|plasma|x11> [OPTIONS]
       ./setup_backend.sh status [all|gnome|plasma|x11]

Configure Docklight integration for one desktop backend.

Backends:
  auto      Detect the current desktop session
  gnome     Install and enable the GNOME Wayland extension
  plasma    Install and enable the KWin Wayland integration
  x11       No companion integration is required

Status:
  status    Report installed and active backend integrations without changes

Plasma options:
  --with-geometry-bridge  Install accurate minimize geometry support
  --with-minimize-effect  Install the Docklight KWin minimize effect

Run this command as the logged-in desktop user, without sudo.
EOF
}

fail()
{
    echo "setup_backend.sh: $*" >&2
    exit 1
}

detect_backend()
{
    local desktop="${XDG_CURRENT_DESKTOP:-}"
    local session_type="${XDG_SESSION_TYPE:-}"

    desktop="${desktop^^}"
    session_type="${session_type,,}"

    if [[ "${session_type}" == "x11" ]]; then
        echo x11
    elif [[ "${session_type}" == "wayland" &&
            "${desktop}" == *GNOME* ]]; then
        echo gnome
    elif [[ "${session_type}" == "wayland" &&
            ("${desktop}" == *KDE* ||
             "${desktop}" == *PLASMA*) ]]; then
        echo plasma
    else
        return 1
    fi
}

yes_or_no()
{
    if "$@"; then
        echo yes
    else
        echo no
    fi
}

gnome_extension_field()
{
    local info="$1"
    local field="$2"

    sed -n \
        "s/^[[:space:]]*${field}:[[:space:]]*//p" \
        <<<"${info}" |
        head -n 1
}

print_gnome_status()
{
    local extension_id="docklight-window-integration@docklight6"
    local development_id="docklight-window-integration-test@docklight6"
    local info=""
    local development_enabled=false

    echo "Backend: GNOME Wayland"

    if ! command -v gnome-extensions >/dev/null 2>&1; then
        echo "  Tooling available: no"
        echo "  Extension installed: unknown"
        return
    fi

    echo "  Tooling available: yes"

    if info="$(LC_ALL=C gnome-extensions info "${extension_id}" 2>/dev/null)"; then
        echo "  Extension installed: yes"
        echo "  Extension enabled: $(
            LC_ALL=C gnome-extensions list --enabled |
                grep -Fqx "${extension_id}" &&
                echo yes || echo no
        )"
        echo "  Extension state: $(
            gnome_extension_field "${info}" State
        )"
        echo "  Extension version: $(
            gnome_extension_field "${info}" Version
        )"
    else
        echo "  Extension installed: no"
        echo "  Extension enabled: no"
        echo "  Extension state: unavailable"
        echo "  Extension version: unavailable"
    fi

    if LC_ALL=C gnome-extensions list --enabled |
        grep -Fqx "${development_id}"
    then
        development_enabled=true
    fi

    echo "  Conflicting development extension enabled: $(
        yes_or_no "${development_enabled}"
    )"
}

plasma_package_installed()
{
    local package_type="$1"
    local package_id="$2"

    kpackagetool6 \
        --type "${package_type}" \
        --show "${package_id}" \
        >/dev/null 2>&1
}

kwin_plugin_enabled()
{
    local plugin_id="$1"

    command -v kreadconfig6 >/dev/null 2>&1 &&
        [[ "$(
            kreadconfig6 \
                --file kwinrc \
                --group Plugins \
                --key "${plugin_id}Enabled" \
                --default false
        )" == "true" ]]
}

kwin_session_available()
{
    local qdbus_command=""

    if command -v qdbus6 >/dev/null 2>&1; then
        qdbus_command=qdbus6
    elif command -v qdbus >/dev/null 2>&1; then
        qdbus_command=qdbus
    else
        return 1
    fi

    "${qdbus_command}" \
        org.kde.KWin \
        /KWin \
        >/dev/null 2>&1
}

print_plasma_status()
{
    local script_id="org.docklight6.windowintegration"
    local effect_id="org.docklight6.minimize"
    local bridge_id="org.docklight6.geometrybridge"

    echo "Backend: Plasma Wayland"

    if ! command -v kpackagetool6 >/dev/null 2>&1; then
        echo "  Tooling available: no"
        echo "  KWin integration installed: unknown"
        return
    fi

    echo "  Tooling available: yes"
    echo "  KWin session active: $(yes_or_no kwin_session_available)"
    echo "  KWin integration installed: $(
        yes_or_no plasma_package_installed KWin/Script "${script_id}"
    )"
    echo "  KWin integration enabled: $(
        yes_or_no kwin_plugin_enabled "${script_id}"
    )"
    echo "  Geometry bridge installed: $(
        yes_or_no plasma_package_installed Plasma/Applet "${bridge_id}"
    )"
    echo "  Minimize effect installed: $(
        yes_or_no plasma_package_installed KWin/Effect "${effect_id}"
    )"
    echo "  Minimize effect enabled: $(
        yes_or_no kwin_plugin_enabled "${effect_id}"
    )"
}

print_x11_status()
{
    local active=false

    if [[ "${XDG_SESSION_TYPE:-}" == "x11" ]]; then
        active=true
    fi

    echo "Backend: X11/EWMH"
    echo "  Built into Docklight: yes"
    echo "  Current X11 session: $(yes_or_no "${active}")"
    echo "  Companion integration required: no"
}

print_status()
{
    local target="${1:-all}"
    local current="unsupported"

    current="$(detect_backend 2>/dev/null || echo unsupported)"
    echo "Current session backend: ${current}"

    case "${target}" in
    all)
        echo
        print_gnome_status
        echo
        print_plasma_status
        echo
        print_x11_status
        ;;
    gnome)
        print_gnome_status
        ;;
    plasma)
        print_plasma_status
        ;;
    x11)
        print_x11_status
        ;;
    *)
        fail "unknown status backend: ${target}"
        ;;
    esac
}

if ((EUID == 0)); then
    fail "run backend setup as the desktop user, without sudo"
fi

if (($# == 0)); then
    usage >&2
    exit 2
fi

if [[ "$1" == "--help" || "$1" == "-h" ]]; then
    usage
    exit 0
fi

if [[ "$1" == "status" ]]; then
    if (($# > 2)); then
        fail "status accepts at most one backend"
    fi
    print_status "${2:-all}"
    exit 0
fi

backend="$1"
shift

install_geometry_bridge=false
install_minimize_effect=false

while (($# > 0)); do
    case "$1" in
    --with-geometry-bridge)
        install_geometry_bridge=true
        ;;
    --with-minimize-effect)
        install_minimize_effect=true
        ;;
    --help|-h)
        usage
        exit 0
        ;;
    *)
        fail "unknown option: $1"
        ;;
    esac
    shift
done

if [[ "${backend}" == "auto" ]]; then
    if ! backend="$(detect_backend)"; then
        fail "cannot select a backend for XDG_CURRENT_DESKTOP='${XDG_CURRENT_DESKTOP:-}' and XDG_SESSION_TYPE='${XDG_SESSION_TYPE:-}'"
    fi
    echo "Detected Docklight backend: ${backend}"
fi

if [[ "${backend}" != "plasma" ]] &&
   { "${install_geometry_bridge}" ||
     "${install_minimize_effect}"; }; then
    fail "Plasma integration options require the plasma backend"
fi

case "${backend}" in
gnome)
    "${SCRIPT_DIRECTORY}/gnome/install-window-integration.sh"
    ;;
plasma)
    "${SCRIPT_DIRECTORY}/kwin/install-window-integration.sh"

    if "${install_geometry_bridge}"; then
        "${SCRIPT_DIRECTORY}/plasma/geometry-bridge/install-geometry-bridge.sh"
    fi

    if "${install_minimize_effect}"; then
        "${SCRIPT_DIRECTORY}/kwin/install-minimize-effect.sh"
    fi
    ;;
x11)
    echo "Docklight X11 integration uses the window manager's EWMH interface; no companion installation is required"
    ;;
*)
    fail "unknown backend: ${backend}"
    ;;
esac

echo "Docklight backend setup complete: ${backend}"
echo
print_status "${backend}"
