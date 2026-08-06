#!/usr/bin/env bash

set -euo pipefail

SOURCE_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
DOCKLIGHT_BUILD_DIR=${DOCKLIGHT_BUILD_DIR:-"$SOURCE_DIR/build"}

if [[ $DOCKLIGHT_BUILD_DIR != /* ]]; then
    DOCKLIGHT_BUILD_DIR="$SOURCE_DIR/$DOCKLIGHT_BUILD_DIR"
fi

DOCKLIGHT_BUILD_DIR=$(realpath -m -- "$DOCKLIGHT_BUILD_DIR")

case $DOCKLIGHT_BUILD_DIR in
    "$SOURCE_DIR"|/)
        echo "Refusing to remove unsafe build directory: $DOCKLIGHT_BUILD_DIR" >&2
        exit 1
        ;;
esac

if [[ ! -d $DOCKLIGHT_BUILD_DIR ]]; then
    echo "Build directory does not exist: $DOCKLIGHT_BUILD_DIR"
    exit 0
fi

rm -rf -- "$DOCKLIGHT_BUILD_DIR"
echo "Removed build directory: $DOCKLIGHT_BUILD_DIR"
