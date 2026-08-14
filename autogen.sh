#!/bin/sh

set -eu

SOURCE_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DOCKLIGHT_BUILD_DIR=${DOCKLIGHT_BUILD_DIR:-"$SOURCE_DIR/build"}

# Keep the everyday build suitable for debugging. Explicit caller-provided
# flags still take precedence.
CFLAGS=${CFLAGS:-"-O0 -g3"}
CXXFLAGS=${CXXFLAGS:-"-O0 -g3 -DDEBUG"}
export CFLAGS CXXFLAGS

case $DOCKLIGHT_BUILD_DIR in
    /*) ;;
    *) DOCKLIGHT_BUILD_DIR="$SOURCE_DIR/$DOCKLIGHT_BUILD_DIR" ;;
esac

DOCKLIGHT_BUILD_DIR=$(realpath -m -- "$DOCKLIGHT_BUILD_DIR")

case $DOCKLIGHT_BUILD_DIR in
    "$SOURCE_DIR"|/)
        echo "Refusing unsafe build directory: $DOCKLIGHT_BUILD_DIR" >&2
        exit 1
        ;;
esac

cd "$SOURCE_DIR"
autoreconf --install --force --verbose

mkdir -p "$DOCKLIGHT_BUILD_DIR"
cd "$DOCKLIGHT_BUILD_DIR"

"$SOURCE_DIR/configure" "$@"
