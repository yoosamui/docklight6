#!/usr/bin/env bash

if [ $EUID != 0 ]; then
	echo "this script must be run as root"
	echo ""
	echo "usage:"
	echo "sudo "$0
	exit $exit_code
   exit 1
fi

./autogen.sh
./configure
sudo make install

# Older development installs may have left a user-level desktop entry with a
# relative or build-tree Exec path. Because user entries override /usr/local,
# update that entry when it exists so KWin resolves the same executable and
# permissions as the system installation.
if [ -n "$SUDO_USER" ]; then
	DOCKLIGHT_USER_HOME=$(getent passwd "$SUDO_USER" | cut -d: -f6)
	DOCKLIGHT_USER_DESKTOP="$DOCKLIGHT_USER_HOME/.local/share/applications/org.docklight6.desktop"

	if [ -f "$DOCKLIGHT_USER_DESKTOP" ]; then
		sudo -u "$SUDO_USER" install -m 0644 \
			data/org.docklight6.desktop \
			"$DOCKLIGHT_USER_DESKTOP"
	fi
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
