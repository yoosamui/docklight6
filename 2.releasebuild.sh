#!/usr/bin/env bash

set -euo pipefail

SOURCE_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
DOCKLIGHT_BUILD_DIR=${DOCKLIGHT_BUILD_DIR:-"$SOURCE_DIR/build"}
RELEASE_CFLAGS="-O2 -DNDEBUG"
RELEASE_CXXFLAGS="-O2 -DNDEBUG"

if [[ $DOCKLIGHT_BUILD_DIR != /* ]]; then
    DOCKLIGHT_BUILD_DIR="$SOURCE_DIR/$DOCKLIGHT_BUILD_DIR"
fi

DOCKLIGHT_BUILD_DIR=$(realpath -m -- "$DOCKLIGHT_BUILD_DIR")

# KDE-Plasma WAYLAND
#  KWin authorizes privileged screenshot and screencast access using the executable path registered in:
# /usr/local/share/applications/org.docklight6.desktop


# That desktop file currently contains:
# Exec=/usr/local/bin/docklight6
# X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2
# X-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1

# Therefore /usr/local/bin/docklight6 has permission, while
# ./build/src/docklight6 is considered a different, unregistered executable.
# Running the build-tree binary directly will lose KWin authorization,
# usually resulting in icons instead of thumbnails.
# For development, keep using:
# sudo install -m 0755 build/src/docklight6 /usr/local/bin/docklight6
# /usr/local/bin/docklight6
# This copies the latest build to the authorized path. It is a KDE Wayland security requirement, not a DockLight loading problem.



if [[ ! -f "$DOCKLIGHT_BUILD_DIR/Makefile" ]] ||
   ! grep -Fqx "CFLAGS = $RELEASE_CFLAGS" "$DOCKLIGHT_BUILD_DIR/Makefile" ||
   ! grep -Fqx "CXXFLAGS = $RELEASE_CXXFLAGS" "$DOCKLIGHT_BUILD_DIR/Makefile"; then
    DOCKLIGHT_BUILD_DIR="$DOCKLIGHT_BUILD_DIR" \
        "$SOURCE_DIR/clean.sh"
    CFLAGS="$RELEASE_CFLAGS" \
        CXXFLAGS="$RELEASE_CXXFLAGS" \
        DOCKLIGHT_BUILD_DIR="$DOCKLIGHT_BUILD_DIR" \
        "$SOURCE_DIR/autogen.sh"
fi

make -C "$DOCKLIGHT_BUILD_DIR" -j"$(nproc)"
sudo install -m 0755 \
    "$DOCKLIGHT_BUILD_DIR/src/docklight6" \
    /usr/local/bin/docklight6
pkill docklight6 || true
exec /usr/local/bin/docklight6
