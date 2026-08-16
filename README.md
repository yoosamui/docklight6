


# DockLight 6


<img width="127" height="127" alt="docklight home" src="https://github.com/user-attachments/assets/7e338d8e-2ce2-4ede-b8dc-48aed38e25d2" />

DockLight is a lightweight application dock for Linux with X11 and Wayland
support. It provides application launchers, window management, live window
previews, multi-monitor placement, and configurable auto-hide behavior.

**Current version:** `6.0.32`

## Integration tests

`PASS` means confirmed in a real desktop session. `PENDING` means the backend
is implemented but still needs real-session verification. `NOT IMPLEMENTED`
means DockLight can start, but desktop-specific window integration is not
available.

| Desktops | Session | WM | Compositor | Selected backend | Result | Comments |
|---|---|---|---|---|---|---|
| KDE Plasma | Wayland | KWin | KWin | `KWinWindowBackend` | PASS | Uses the KWin script and D-Bus integration. |
| KDE Plasma | X11 | KWin | KWin | `KWinX11WindowBackend` | PASS | Uses native X11/EWMH window integration. |
| XFCE | X11 | xfwm4 | xfwm4 | `Xfwm4WindowBackend` | PASS | Uses the desktop-specific X11 backend. |
| MATE | X11 | Marco or Metacity | Marco or Metacity | `MarcoWindowBackend` | PASS | Marco and Metacity share this X11 backend. |
| Cinnamon | X11 | Muffin | Muffin | `MuffinWindowBackend` | PASS | Uses the desktop-specific X11 backend. |
| GNOME | X11 | Mutter / GNOME Shell | Mutter | `GnomeWaylandWindowBackend` | PASS | Uses the GNOME Shell extension bridge. |
| GNOME | Wayland | Mutter / GNOME Shell | Mutter | `GnomeWaylandWindowBackend` | PASS | Uses the GNOME Shell extension bridge. |
| LXDE | X11 | Openbox | compton or picom | `OpenboxWindowBackend` | PASS | A compositor is required for complete window previews. |
| LXQt | X11 | Openbox | compton or picom | `OpenboxWindowBackend` | PENDING | Implemented; real-session verification remains. |
| Other EWMH desktops | X11 | Other EWMH WM | Any or none | `EwmhFallbackWindowBackend` | PASS | Generic EWMH-compatible fallback. |
| XFCE | Wayland | N/A | Varies | None | NOT IMPLEMENTED | No supported Wayland integration backend. |
| Sway | Wayland | N/A | Sway / wlroots | None | NOT IMPLEMENTED | No supported Wayland integration backend. |
| Hyprland | Wayland | N/A | Hyprland | None | NOT IMPLEMENTED | No supported Wayland integration backend. |
| Wayfire | Wayland | N/A | Wayfire / wlroots | None | NOT IMPLEMENTED | No supported Wayland integration backend. |
| labwc | Wayland | N/A | labwc / wlroots | None | NOT IMPLEMENTED | No supported Wayland integration backend. |
| COSMIC | Wayland | N/A | COSMIC compositor | None | NOT IMPLEMENTED | No supported Wayland integration backend. |
| Generic or other desktop | Wayland | N/A | Other Wayland compositor | None | NOT IMPLEMENTED | No supported Wayland integration backend. |

Important current-code details:

- KWin, xfwm4, Marco/Metacity, Muffin, Mutter, and Openbox have separate X11
  backends. GNOME Shell sessions use `GnomeWaylandWindowBackend`; the X11
  `MutterWindowBackend` is selected only when Mutter is detected outside a
  GNOME Shell desktop identity.
- Openbox window previews require an X11 compositor such as `compton` or
  `picom`. Without one, DockLight continues running, sets `display_preview`
  to `false`, and displays a warning.
- KWin on X11 selects `KWinX11WindowBackend`; unknown EWMH-compatible window
  managers select `EwmhFallbackWindowBackend`.
- KDE Plasma/KWin and GNOME Shell are the enabled Wayland environments.
- Other Wayland sessions exit window-integration startup without creating a
  backend.

**Project status:** Feature complete. Development now focuses on maintenance,
bug fixes, compatibility, and translations.

DockLight's complete window-management integration is designed for X11 or
Wayland. On unsupported desktop sessions, the dock can still start, but
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
./build.sh debug --clean --check
./build.sh release --clean --install
```

The build script requests `sudo` only for the installation step. The core
installation does not install a desktop integration. Configure the current
desktop integration separately as the logged-in desktop user (without
`sudo`):

```sh
./setup_backend.sh auto
```

Backend integrations can coexist for a user. Install them explicitly when the
same system is used with more than one desktop:

```sh
./setup_backend.sh gnome
./setup_backend.sh plasma
./setup_backend.sh x11
```

The GNOME command installs and enables the Shell extension. A newly installed
or updated GNOME extension may require one logout and login before Shell can
activate it. The Plasma command installs and enables the required KWin script.
X11 uses EWMH directly and needs no companion component.

Inspect every available integration without changing the system:

```sh
./setup_backend.sh status
```

The setup command also prints the selected backend's installed, enabled, and
active state after every installation or update.

DockLight can then be launched from the application menu or with:

```sh
docklight6
```

### Development builds

DockLight uses an out-of-source Autotools build, keeping generated files out of
`src/`. Create a debug build with full debug symbols and run its tests with:

```sh
./build.sh debug --check
./build-debug/src/docklight6
```

Create an optimized release build with assertions and debug-only logging
disabled:

```sh
./build.sh release
./build-release/src/docklight6
```

Run tests against the debug build: release mode defines `NDEBUG`, which
disables the C++ suite's `assert()` checks.

Clean and rebuild either configuration with:

```sh
./build.sh debug --clean
./build.sh release --clean
```

Use `./build.sh --help` for installation, run, restart, GDB, job-count, and
clean-only options. See [SETUP.md](SETUP.md) for the complete development and
script reference.

Install the KWin integration separately when testing window management on KDE
Plasma Wayland:

```sh
./setup_backend.sh plasma
```

## Optional Plasma integration

DockLight detects and activates installed companion components after the KWin
connection is established. Install them manually to enable the custom minimize
animation and accurate minimize geometry:

```sh
sudo apt install qt6-base-dev qt6-declarative-dev
```

```sh
./setup_backend.sh plasma \
    --with-minimize-effect \
    --with-geometry-bridge
```

The setup command must be run as the logged-in Plasma user, without `sudo`.

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
