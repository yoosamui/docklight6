# GNOME Wayland integration

GNOME Wayland does not expose other applications' windows to ordinary
Wayland clients. Docklight therefore uses a small GNOME Shell extension to
publish normalized Mutter window state over its private session D-Bus
protocol and to execute activate, raise, close, minimize, maximize, present,
and hide commands.

Build Docklight normally, then install the extension as the logged-in desktop
user (without `sudo`):

```sh
make install-gnome-integration
```

GNOME Shell does not discover a brand-new local extension during an existing
Wayland session. On the first installation, log out and back in once and run:

```sh
gnome-extensions enable docklight-window-integration@docklight6
```

Updates to an extension that GNOME already knows about are installed by the
same make target. The extension supports GNOME Shell 45 through 48.
