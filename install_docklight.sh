#!/usr/bin/env bash

set -e

SOURCE_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
DOCKLIGHT_BUILD_DIR=${DOCKLIGHT_BUILD_DIR:-"$SOURCE_DIR/build"}

if [[ $DOCKLIGHT_BUILD_DIR != /* ]]; then
	DOCKLIGHT_BUILD_DIR="$SOURCE_DIR/$DOCKLIGHT_BUILD_DIR"
fi

DOCKLIGHT_BUILD_DIR=$(realpath -m -- "$DOCKLIGHT_BUILD_DIR")

cd "$SOURCE_DIR"

if [ $EUID != 0 ]; then
	echo "this script must be run as root"
	echo ""
	echo "usage:"
	echo "sudo "$0
	exit $exit_code
   exit 1
fi

./autogen.sh
make -C "$DOCKLIGHT_BUILD_DIR" -j"$(nproc)"
make -C "$DOCKLIGHT_BUILD_DIR" install

# Older development installs may have left a user-level desktop entry with a
# relative or build-tree Exec path. Because user entries override /usr/local,
# update that entry when it exists so KWin resolves the same executable and
# permissions as the system installation.
if [ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != "root" ]; then
    DOCKLIGHT_USER_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
    DOCKLIGHT_USER_ID=$(id -u "$SUDO_USER")
    DOCKLIGHT_USER_APPLICATIONS="$DOCKLIGHT_USER_HOME/.local/share/applications"
    DOCKLIGHT_USER_DESKTOP="$DOCKLIGHT_USER_APPLICATIONS/org.docklight6.desktop"

    sudo -H -u "$SUDO_USER" \
        mkdir -p "$DOCKLIGHT_USER_APPLICATIONS"

    sudo -H -u "$SUDO_USER" \
        install -m 0644 \
        data/org.docklight6.desktop \
        "$DOCKLIGHT_USER_DESKTOP"

    sudo -H -u "$SUDO_USER" env \
        HOME="$DOCKLIGHT_USER_HOME" \
        XDG_DATA_DIRS="/usr/local/share:/usr/share" \
        XDG_RUNTIME_DIR="/run/user/$DOCKLIGHT_USER_ID" \
        DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$DOCKLIGHT_USER_ID/bus" \
        kbuildsycoca6 --noincremental
else
    echo "Warning: cannot register DockLight for the desktop user."
    echo "Run this installer through sudo from the Plasma user account."
fi

# KWin authorizes restricted Wayland interfaces through the installed desktop
# entry. Refresh KDE's per-user service cache so the screencast permission is
# visible immediately instead of only after the next login.
if command -v kbuildsycoca6 >/dev/null 2>&1; then
	if [ -n "$SUDO_USER" ]; then
		DOCKLIGHT_USER_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
		DOCKLIGHT_USER_ID=$(id -u "$SUDO_USER")
		sudo -H -u "$SUDO_USER" env \
			HOME="$DOCKLIGHT_USER_HOME" \
			XDG_DATA_DIRS="/usr/local/share:/usr/share" \
			XDG_RUNTIME_DIR="/run/user/$DOCKLIGHT_USER_ID" \
			DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/$DOCKLIGHT_USER_ID/bus" \
			kbuildsycoca6 --noincremental
	else
		kbuildsycoca6 --noincremental
	fi
fi

cd po
./deploymo.sh
