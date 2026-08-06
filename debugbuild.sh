#!/usr/bin/env bash

set -euo pipefail

SOURCE_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
cd "$SOURCE_DIR"

./clean.sh
CXXFLAGS="-DDEBUG" ./autogen.sh
make -C build -j"$(nproc)"
