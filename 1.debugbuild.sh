#!/usr/bin/env bash

set -euo pipefail

SOURCE_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
DOCKLIGHT_BUILD_DIR=${DOCKLIGHT_BUILD_DIR:-"$SOURCE_DIR/build-debug"}
cd "$SOURCE_DIR"

DOCKLIGHT_BUILD_DIR="$DOCKLIGHT_BUILD_DIR" ./clean.sh
CFLAGS="-O0 -g3" \
    CXXFLAGS="-O0 -g3 -DDEBUG" \
    DOCKLIGHT_BUILD_DIR="$DOCKLIGHT_BUILD_DIR" \
    ./autogen.sh
make -C "$DOCKLIGHT_BUILD_DIR" -j"$(nproc)"
sudo install -m 0755 \
    "$DOCKLIGHT_BUILD_DIR/src/docklight6" \
    /usr/local/bin/docklight6
pkill docklight6 || true
exec gdb /usr/local/bin/docklight6
