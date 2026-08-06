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
