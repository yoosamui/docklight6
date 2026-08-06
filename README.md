# DockLight 6

DockLight is a lightweight application dock for Linux with X11 and Wayland
support. It provides application launchers, window management, live window
previews, multi-monitor placement, and configurable auto-hide behavior.

**Current version:** `6.0.28` Only KDE-Plasma Wayland

**Project status:** Feature complete. Development now focuses on maintenance,
bug fixes, compatibility, and translations.

DockLight's complete window-management integration is designed for KDE Plasma
6 on Wayland. On unsupported desktop sessions, the dock can still start, but
desktop-specific window actions and previews may not be available.

## Architecture

DockLight is a C++17 GTK application built with gtkmm 3. Its main components
are separated by responsibility:

- `src/dock/` contains the dock window, items, tooltips, and UI controller.
- `src/application/` maps dock items to running applications and coordinates
  window actions.
- `src/windowing/` provides desktop-independent window models and the window
  registry.
- `src/integrations/` connects the registry to KWin through D-Bus and manages
  the Plasma-specific integration components.
- `src/layout/`, `src/autohide/`, and `src/rendering/` calculate dock geometry,
  visibility behavior, and icon presentation.
- `src/preview/` captures and displays live window previews through PipeWire
  and KDE's Wayland screencast protocol.
- `src/config/` loads, validates, saves, and watches the per-user
  configuration; changes are applied while DockLight is running.
- `kwin/` and `plasma/` contain the companion KWin scripts, effect, and Plasma
  geometry bridge used for window tracking and desktop integration.

At startup, `main.cpp` loads configuration and monitor state, starts the
window-system integration, and creates the dock window. The UI talks to
applications through `DockApplicationController` and `WindowRegistry`, so
desktop-specific KWin code remains isolated from the dock widgets.

## Installation

### Debian and Ubuntu based systems

Install the build and runtime dependencies:

```sh
sudo ./install_dependencies.sh
```

Build and install DockLight into `/usr/local`:

```sh
sudo ./install_docklight.sh
```

Run the system installer through `sudo` from the Plasma user account. This
allows it to refresh that user's KDE application cache and register the
permissions required for Wayland previews.

Install the KWin window integration as the logged-in Plasma user (do not use
`sudo`):

```sh
make -C build install-kwin-integration
```

The command installs and enables the KWin script for the current user and
reloads KWin. DockLight can then be launched from the application menu or with:

```sh
docklight6
```

### Build without installing

DockLight uses an out-of-source Autotools build, keeping generated files out of
`src/`:

```sh
./autogen.sh
make -C build -j"$(nproc)"
make -C build check
./build/src/docklight6
```

Install the KWin integration separately when testing window management on KDE
Plasma Wayland:

```sh
make -C build install-kwin-integration
```

Use `./clean.sh` to remove the build directory and start a clean build.

## Optional Plasma integration

DockLight detects and activates installed companion components after the KWin
connection is established. Install them manually to enable the custom minimize
animation and accurate minimize geometry:

```sh
sudo apt install qt6-base-dev qt6-declarative-dev
```

```sh
make -C build install-kwin-minimize-effect
make -C build install-plasma-geometry-bridge
```

Both commands must be run as the logged-in Plasma user, without `sudo`.

## Configuration and diagnostics

DockLight creates `~/.config/docklight6/docklight.conf` automatically and
monitors it for changes. Use the settings dialog for normal configuration.

List the monitor names available for monitor-specific placement with:

```sh
docklight6 --list-monitors
```

On KDE Plasma Wayland, a successful integration startup includes these log
messages:

```text
KWin window integration is ready for the KWin script
KWin window integration connected
```

## License

See [LICENSE](LICENSE).
