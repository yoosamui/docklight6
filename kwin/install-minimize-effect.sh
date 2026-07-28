#!/usr/bin/env bash

set -euo pipefail

readonly EFFECT_ID="org.docklight6.minimize"
readonly EFFECT_TYPE="KWin/Effect"
readonly SCRIPT_DIRECTORY="$(
    cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &&
        pwd
)"
readonly PACKAGE_DIRECTORY="${SCRIPT_DIRECTORY}/${EFFECT_ID}"

if ((EUID == 0)); then
    echo "Install the Docklight KWin effect as the Plasma desktop user, without sudo" >&2
    exit 1
fi

for command_name in \
    kpackagetool6 \
    kwriteconfig6 \
    qdbus6
do
    if ! command -v "${command_name}" >/dev/null 2>&1; then
        echo "Required command not found: ${command_name}" >&2
        exit 1
    fi
done

if kpackagetool6 \
    --type "${EFFECT_TYPE}" \
    --show "${EFFECT_ID}" \
    >/dev/null 2>&1
then
    echo "Updating Docklight KWin minimize effect"

    kpackagetool6 \
        --type "${EFFECT_TYPE}" \
        --upgrade "${PACKAGE_DIRECTORY}"
else
    echo "Installing Docklight KWin minimize effect"

    kpackagetool6 \
        --type "${EFFECT_TYPE}" \
        --install "${PACKAGE_DIRECTORY}"
fi

kwriteconfig6 \
    --file kwinrc \
    --group Plugins \
    --key "${EFFECT_ID}Enabled" \
    true

kwriteconfig6 \
    --file kwinrc \
    --group Plugins \
    --key magiclampEnabled \
    false

kwriteconfig6 \
    --file kwinrc \
    --group Plugins \
    --key squashEnabled \
    false

qdbus6 \
    org.kde.KWin \
    /KWin \
    reconfigure

qdbus6 \
    org.kde.KWin \
    /Effects \
    org.kde.kwin.Effects.unloadEffect \
    magiclamp

qdbus6 \
    org.kde.KWin \
    /Effects \
    org.kde.kwin.Effects.unloadEffect \
    squash

qdbus6 \
    org.kde.KWin \
    /Effects \
    org.kde.kwin.Effects.unloadEffect \
    "${EFFECT_ID}"

qdbus6 \
    org.kde.KWin \
    /Effects \
    org.kde.kwin.Effects.loadEffect \
    "${EFFECT_ID}"

echo "Docklight KWin minimize effect is installed and active"
