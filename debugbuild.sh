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
