#!/bin/sh

set -eu

SOURCE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DOCKLIGHT_BUILD_DIR=${DOCKLIGHT_BUILD_DIR:-"$SOURCE_DIR/build"}

case $DOCKLIGHT_BUILD_DIR in
    /*) ;;
    *) DOCKLIGHT_BUILD_DIR="$SOURCE_DIR/$DOCKLIGHT_BUILD_DIR" ;;
esac

DOCKLIGHT_BUILD_DIR=$(realpath -m -- "$DOCKLIGHT_BUILD_DIR")

if [ "$DOCKLIGHT_BUILD_DIR" = "$SOURCE_DIR" ]; then
    echo "The build directory must be separate from the source directory." >&2
    exit 1
fi

cd "$SOURCE_DIR"
autoreconf --install --force --verbose

mkdir -p "$DOCKLIGHT_BUILD_DIR"
cd "$DOCKLIGHT_BUILD_DIR"

"$SOURCE_DIR/configure" "$@"
