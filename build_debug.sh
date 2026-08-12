#!/usr/bin/env bash

set -euo pipefail

SOURCE_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
DOCKLIGHT_BUILD_DIR=${DOCKLIGHT_BUILD_DIR:-"$SOURCE_DIR/build-debug"}
DEBUG_CFLAGS=${DEBUG_CFLAGS:-"-O0 -g3"}
DEBUG_CXXFLAGS=${DEBUG_CXXFLAGS:-"-O0 -g3 -DDEBUG"}

if [[ $DOCKLIGHT_BUILD_DIR != /* ]]; then
    DOCKLIGHT_BUILD_DIR="$SOURCE_DIR/$DOCKLIGHT_BUILD_DIR"
fi

DOCKLIGHT_BUILD_DIR=$(realpath -m -- "$DOCKLIGHT_BUILD_DIR")

DOCKLIGHT_BUILD_DIR="$DOCKLIGHT_BUILD_DIR" "$SOURCE_DIR/clean.sh"
CFLAGS="$DEBUG_CFLAGS" \
    CXXFLAGS="$DEBUG_CXXFLAGS" \
    DOCKLIGHT_BUILD_DIR="$DOCKLIGHT_BUILD_DIR" \
    "$SOURCE_DIR/autogen.sh"
make -C "$DOCKLIGHT_BUILD_DIR" -j"$(nproc)"

sudo install -m 0755 \
    "$DOCKLIGHT_BUILD_DIR/src/docklight6" \
    /usr/local/bin/docklight6
pkill docklight6 || true
exec /usr/local/bin/docklight6

printf 'Debug build created at %s/src/docklight6\n' "$DOCKLIGHT_BUILD_DIR"