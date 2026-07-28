# Docklight Plasma geometry bridge

KWin reads a window's minimize animation destination from Plasma's private
window-management protocol. Plasma Shell already owns that protocol
connection, so Docklight cannot publish the destination directly.

This hidden Plasma applet receives absolute icon rectangles from Docklight
over session D-Bus. It matches each KWin UUID against Plasma's task model and
uses `requestPublishDelegateGeometry()` to publish the rectangle through
Plasma Shell. Its transparent one-pixel panel instance creates the Plasma
surface needed to publish those rectangles.

The bridge is event-driven. It does not poll Docklight or KWin.

The package is marked `NoDisplay`, so it does not appear in Plasma's widget
browser and cannot be added to a panel through the normal user interface. It
has no configuration interface. On startup, Docklight ensures that exactly one
panel instance exists, removes duplicates, and restores the instance if it was
removed.

## Build requirements

- Qt 6 Core development files
- Qt 6 D-Bus development files
- Qt 6 QML development files
- Plasma 6 runtime and `kpackagetool6`

On Debian:

```sh
sudo apt install qt6-base-dev qt6-declarative-dev
```

## Development install

Run as the Plasma desktop user:

```sh
make install-plasma-geometry-bridge
```

Do not use `sudo`. The installer builds the QML plug-in, installs the applet
under the user's Plasma packages, and ensures that one instance exists.

After upgrading an already loaded bridge, restart Plasma Shell once so it
loads the new shared library.

## Production install

Install the bridge system-wide so its package files are managed by the
Docklight installation rather than by an individual user's Plasma setup:

```sh
sudo make install-plasma-geometry-bridge-system
```

The global installer does not access a user's Plasma session. The next time
Docklight starts for a Plasma user, it creates or repairs the hidden bridge
instance automatically.

If the development package was installed previously, remove that user-local
copy before relying on the system package, because a package in
`~/.local/share/plasma/plasmoids` takes precedence over the system copy.

Plasma does not provide a supported per-widget flag that makes one panel
instance permanently non-removable while the panel remains editable. Hiding
the package, managing its files system-wide, and restoring one instance when
Docklight starts provides the intended infrastructure behavior without
locking the user's entire panel.
