#!/usr/bin/env bash

set -euo pipefail

SOURCE_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
DOCKLIGHT_BUILD_DIR=${DOCKLIGHT_BUILD_DIR:-"$SOURCE_DIR/build"}

if [[ $DOCKLIGHT_BUILD_DIR != /* ]]; then
	DOCKLIGHT_BUILD_DIR="$SOURCE_DIR/$DOCKLIGHT_BUILD_DIR"
fi

DOCKLIGHT_BUILD_DIR=$(realpath -m -- "$DOCKLIGHT_BUILD_DIR")

cd "$SOURCE_DIR"

if ((EUID != 0)); then
    echo "This script installs the Docklight core and must be run as root." >&2
    echo "Usage: sudo $0" >&2
    exit 1
fi

./autogen.sh
make -C "$DOCKLIGHT_BUILD_DIR" -j"$(nproc)"
make -C "$DOCKLIGHT_BUILD_DIR" install

cd po
./deploymo.sh

echo
echo "Docklight core installation is complete."
echo "As the desktop user, configure an integration with:"
echo "  ./setup_backend.sh auto"
