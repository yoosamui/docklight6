# Docklight Plasma geometry bridge

KWin reads a window's minimize animation destination from Plasma's private
window-management protocol. Plasma Shell already owns that protocol
connection, so Docklight cannot publish the destination directly.

This hidden Plasma applet receives absolute icon rectangles from Docklight
over session D-Bus. It matches each KWin UUID against Plasma's task model and
uses `requestPublishDelegateGeometry()` to publish the rectangle through
Plasma Shell.

The bridge is event-driven. It does not poll Docklight or KWin.

## Build requirements

- Qt 6 Core development files
- Qt 6 D-Bus development files
- Qt 6 QML development files
- Plasma 6 runtime and `kpackagetool6`

On Debian:

```sh
sudo apt install qt6-base-dev qt6-declarative-dev
```

## Install

Run as the Plasma desktop user:

```sh
make install-plasma-geometry-bridge
```

Do not use `sudo`. The installer builds the QML plug-in, installs the applet
under the user's Plasma packages, and adds one transparent 1x1 instance to the
Plasma panel containing the task manager. Sharing that panel's Wayland surface
allows Docklight's geometry to replace the task manager geometry for the same
window.

After upgrading an already loaded bridge, restart Plasma Shell once so it
loads the new shared library.
