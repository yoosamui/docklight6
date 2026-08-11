#!/usr/bin/env bash

set -euo pipefail

readonly SCRIPT_ID="org.docklight6.windowintegration"
readonly SCRIPT_TYPE="KWin/Script"
readonly SCRIPT_DIRECTORY="$(
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &&
        pwd
)"
readonly PACKAGE_DIRECTORY="${SCRIPT_DIRECTORY}/${SCRIPT_ID}"

if ((EUID == 0)); then
    echo "Install the KWin integration as the Plasma desktop user, without sudo" >&2
    exit 1
fi

require_command()
{
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Required command not found: $1" >&2
        exit 1
    fi
}

require_command kpackagetool6
require_command kwriteconfig6
require_command kbuildsycoca6

if command -v qdbus6 >/dev/null 2>&1; then
    readonly QDBUS_COMMAND="qdbus6"
elif command -v qdbus >/dev/null 2>&1; then
    readonly QDBUS_COMMAND="qdbus"
else
    echo "Required command not found: qdbus6 or qdbus" >&2
    exit 1
fi

if kpackagetool6 \
    --type "${SCRIPT_TYPE}" \
    --show "${SCRIPT_ID}" \
    >/dev/null 2>&1
then
    echo "Updating Docklight KWin window integration"

    kpackagetool6 \
        --type "${SCRIPT_TYPE}" \
        --upgrade "${PACKAGE_DIRECTORY}"
else
    echo "Installing Docklight KWin window integration"

    kpackagetool6 \
        --type "${SCRIPT_TYPE}" \
        --install "${PACKAGE_DIRECTORY}"
fi

# KWin authorizes restricted Wayland interfaces through the desktop entry.
# Keep the per-user copy synchronized with the installed core and refresh the
# KDE service cache here, where the active Plasma user and session are known.
readonly USER_APPLICATIONS_DIRECTORY="${XDG_DATA_HOME:-${HOME}/.local/share}/applications"
readonly USER_DESKTOP_ENTRY="${USER_APPLICATIONS_DIRECTORY}/org.docklight6.desktop"

mkdir -p "${USER_APPLICATIONS_DIRECTORY}"
install -m 0644 \
    "${SCRIPT_DIRECTORY}/../data/org.docklight6.desktop" \
    "${USER_DESKTOP_ENTRY}"

XDG_DATA_DIRS="/usr/local/share:${XDG_DATA_DIRS:-/usr/share}" \
    kbuildsycoca6 --noincremental

kwriteconfig6 \
    --file kwinrc \
    --group Plugins \
    --key "${SCRIPT_ID}Enabled" \
    true

"${QDBUS_COMMAND}" \
    org.kde.KWin \
    /KWin \
    reconfigure

echo "Docklight KWin window integration is installed and enabled"
