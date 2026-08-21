# GNOME Wayland integration

## XWayland presentation

Configure the presentation choice without changing the GNOME integration:

```bash
./setup_presentation.sh auto
./setup_presentation.sh xwayland
./setup_presentation.sh status
./setup_presentation.sh native
```

`auto` is recommended and is the default: it selects XWayland in a GNOME
Wayland session when available, while retaining native presentation on Plasma
Wayland and X11.

Validate a running GNOME/XWayland instance and its EWMH dock contract with:

```bash
./validate_presentation.sh
```

GNOME Wayland does not expose other applications' windows to ordinary
Wayland clients. Docklight therefore uses a small GNOME Shell extension to
publish normalized Mutter window state over its private session D-Bus
protocol and to execute activate, raise, close, minimize, maximize, present,
and hide commands. The extension also places Docklight's own surface at its
configured monitor edge, centres its dialogs, respects existing GNOME panel
and dock work areas, and reserves space while autohide is disabled; ordinary
applications cannot perform those operations themselves on GNOME Wayland.

From the source directory, install the extension as the logged-in desktop
user (without `sudo`):

```sh
./setup_backend.sh gnome
```

The backend dispatcher calls `gnome/install-window-integration.sh`. The
equivalent Autotools target is also available from a configured build
directory, for example `make -C build install-gnome-integration`.

GNOME Shell does not discover a brand-new local extension or reload an updated
ES module during an existing Wayland session. After installing or updating,
log out and back in once and run:

```sh
gnome-extensions enable docklight-window-integration@docklight6
```

Updates to an extension that GNOME already knows about are installed by the
same make target, but still take effect after the next login. The extension
supports GNOME Shell 45 through 48.
