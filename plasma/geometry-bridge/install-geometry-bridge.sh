#!/bin/bash

set -euo pipefail

if [ "${EUID}" -eq 0 ]; then
    echo "Install the Plasma geometry bridge as the desktop user, without sudo" >&2
    exit 1
fi

for command_name in cmake kpackagetool6 qdbus6; do
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
installed_package="${HOME}/.local/share/plasma/plasmoids/${package_id}"

if [ -d "${installed_package}" ]; then
    echo "Updating Docklight Plasma geometry bridge"
    kpackagetool6 \
        --type Plasma/Applet \
        --upgrade "${package_directory}"
else
    echo "Installing Docklight Plasma geometry bridge"
    kpackagetool6 \
        --type Plasma/Applet \
        --install "${package_directory}"
fi

plasma_script='
const packageId = "org.docklight6.geometrybridge";
const taskManagerIds = [
    "org.kde.plasma.icontasks",
    "org.kde.plasma.taskmanager"
];
const panelContainments = panels();
const allContainments = desktops().concat(panelContainments);

let targetPanel = null;
let installedWidget = null;
let installedContainment = null;

for (const panel of panelContainments) {
    for (const widgetId of panel.widgetIds) {
        const widget = panel.widgetById(widgetId);

        if (taskManagerIds.includes(widget.type)) {
            targetPanel = panel;
            break;
        }
    }

    if (targetPanel) {
        break;
    }
}

for (const containment of allContainments) {
    for (const widgetId of containment.widgetIds) {
        const widget = containment.widgetById(widgetId);

        if (widget.type === packageId) {
            installedWidget = widget;
            installedContainment = containment;
            break;
        }
    }

    if (installedWidget) {
        break;
    }
}

if (!targetPanel && panelContainments.length > 0) {
    targetPanel = panelContainments[0];
}

if (!targetPanel) {
    throw new Error(
        "No Plasma panel is available for the Docklight geometry bridge");
}

if (installedWidget) {
    installedWidget.remove();
    installedWidget = null;
}

targetPanel.addWidget(packageId);
'

qdbus6 \
    org.kde.plasmashell \
    /PlasmaShell \
    org.kde.PlasmaShell.evaluateScript \
    "${plasma_script}" >/dev/null

echo "Docklight Plasma geometry bridge is installed and active"
