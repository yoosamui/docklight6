#!/bin/bash

# This happens because KWin authorizes privileged screenshot and screencast access using the executable path registered in:
# /usr/local/share/applications/org.docklight6.desktop


# That desktop file currently contains:
# Exec=/usr/local/bin/docklight6
# X-KDE-DBUS-Restricted-Interfaces=org.kde.KWin.ScreenShot2
# X-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1

# Therefore /usr/local/bin/docklight6 has permission, while ./src/docklight6 is considered a different, unregistered executable. Running the build-tree binary directly will lose KWin authorization, usually resulting in icons instead of thumbnails.
# For development, keep using:
# sudo install -m 0755 src/docklight6 /usr/local/bin/docklight6
# /usr/local/bin/docklight6
# This copies the latest build to the authorized path. It is a KDE Wayland security requirement, not a DockLight loading problem.



make -C src -j"$(nproc)" docklight6 &&
sudo install -m 0755 src/docklight6 /usr/local/bin/docklight6 &&
pkill docklight6
/usr/local/bin/docklight6
