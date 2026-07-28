#!/bin/bash

set -euo pipefail

install_globally=false

if [ "${1:-}" = "--global" ]; then
    install_globally=true
    shift
fi

if [ "$#" -ne 0 ]; then
    echo "Usage: $0 [--global]" >&2
    exit 1
fi

if "${install_globally}"; then
    if [ "${EUID}" -ne 0 ]; then
        echo "The global Plasma geometry bridge installation requires root" >&2
        exit 1
    fi
elif [ "${EUID}" -eq 0 ]; then
    echo "Install the development Plasma geometry bridge as the desktop user, without sudo" >&2
    exit 1
fi

required_commands=(
    cmake
    kpackagetool6
)

if ! "${install_globally}"; then
    required_commands+=(
        qdbus6
    )
fi

for command_name in "${required_commands[@]}"; do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "Missing required command: ${command_name}" >&2
        exit 1
    fi
done

script_directory="$(
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &&
        pwd
)"

build_directory="$(
    mktemp -d \
        "${TMPDIR:-/tmp}/docklight-geometry-build.XXXXXX"
)"

package_directory="$(
    mktemp -d \
        "${TMPDIR:-/tmp}/docklight-geometry-package.XXXXXX"
)"

cleanup()
{
    rm -rf -- \
        "${build_directory}" \
        "${package_directory}"
}

trap cleanup EXIT

if ! cmake \
    -S "${script_directory}/plugin" \
    -B "${build_directory}" \
    -DCMAKE_BUILD_TYPE=Release; then
    echo >&2
    echo "The geometry bridge requires the Qt 6 development packages." >&2
    echo "On Debian, install: qt6-base-dev qt6-declarative-dev" >&2
    exit 1
fi

cmake --build \
    "${build_directory}" \
    --parallel

cp -a \
    "${script_directory}/package/." \
    "${package_directory}/"

cp \
    "${build_directory}/libdocklightgeometrybridgeplugin.so" \
    "${package_directory}/contents/ui/bridge/"

package_id="org.docklight6.geometrybridge"
package_tool_arguments=(
    --type Plasma/Applet
)

if "${install_globally}"; then
    package_tool_arguments+=(
        --global
    )
fi

if kpackagetool6 \
    "${package_tool_arguments[@]}" \
    --show "${package_id}" \
    >/dev/null 2>&1; then
    echo "Updating Docklight Plasma geometry bridge"
    kpackagetool6 \
        "${package_tool_arguments[@]}" \
        --upgrade "${package_directory}"
else
    echo "Installing Docklight Plasma geometry bridge"
    kpackagetool6 \
        "${package_tool_arguments[@]}" \
        --install "${package_directory}"
fi

if "${install_globally}"; then
    echo "Docklight Plasma geometry bridge is installed system-wide"
    echo "Start Docklight as the Plasma desktop user to activate the bridge"
else
    plasma_script="$(
        <"${script_directory}/ensure-geometry-bridge.js"
    )"

    qdbus6 \
        org.kde.plasmashell \
        /PlasmaShell \
        org.kde.PlasmaShell.evaluateScript \
        "${plasma_script}" >/dev/null

    echo "Docklight Plasma geometry bridge is installed and active"
fi
