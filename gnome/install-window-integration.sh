#!/usr/bin/env bash

set -euo pipefail

readonly EXTENSION_ID="docklight-window-integration@docklight6"
readonly SCRIPT_DIRECTORY="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly SOURCE_DIRECTORY="${SCRIPT_DIRECTORY}/${EXTENSION_ID}"

if ((EUID == 0)); then
    echo "Install the GNOME integration as the desktop user, without sudo" >&2
    exit 1
fi

command -v gnome-extensions >/dev/null 2>&1 || {
    echo "Required command not found: gnome-extensions" >&2
    exit 1
}

readonly PACKAGE_DIRECTORY="$(mktemp -d /tmp/docklight-gnome-extension.XXXXXX)"
trap 'rm -rf -- "${PACKAGE_DIRECTORY}"' EXIT

gnome-extensions pack \
    --force \
    --out-dir "${PACKAGE_DIRECTORY}" \
    --extra-source=placement.js \
    "${SOURCE_DIRECTORY}"

readonly PACKAGE_PATH="${PACKAGE_DIRECTORY}/${EXTENSION_ID}.shell-extension.zip"

gnome-extensions install \
    --force \
    "${PACKAGE_PATH}"

if gnome-extensions info "${EXTENSION_ID}" >/dev/null 2>&1; then
    gnome-extensions enable "${EXTENSION_ID}"
    echo "Docklight GNOME Wayland window integration is installed and enabled"
else
    echo "Docklight GNOME Wayland window integration is installed."
    echo "Log out and back in once, then run:"
    echo "  gnome-extensions enable ${EXTENSION_ID}"
    exit 0
fi
