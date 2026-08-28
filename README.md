


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

<table>
  <thead>
    <tr>
      <th>Desktops</th>
      <th>Session</th>
      <th>WM</th>
      <th>Compositor</th>
      <th>Selected backend</th>
      <th>Result</th>
    </tr>
  </thead>
  <tbody>
    <tr><td>KDE Plasma</td><td>Wayland</td><td>KWin</td><td>KWin</td><td><code>KWinWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Uses the KWin script and D-Bus integration.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>KDE Plasma</td><td>X11</td><td>KWin</td><td>KWin</td><td><code>KWinX11WindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Uses native X11/EWMH window integration.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>XFCE</td><td>X11</td><td>xfwm4</td><td>xfwm4</td><td><code>Xfwm4WindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Uses the desktop-specific X11 backend.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>MATE</td><td>X11</td><td>Marco or Metacity</td><td>Marco or Metacity</td><td><code>MarcoWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Marco and Metacity share this X11 backend.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>Cinnamon</td><td>X11</td><td>Muffin</td><td>Muffin</td><td><code>MuffinWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Uses the desktop-specific X11 backend.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>GNOME</td><td>X11</td><td>Mutter / GNOME Shell</td><td>Mutter</td><td><code>GnomeWaylandWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Uses the GNOME Shell extension bridge.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>GNOME</td><td>Wayland</td><td>Mutter / GNOME Shell</td><td>Mutter</td><td><code>GnomeWaylandWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Uses the GNOME Shell extension bridge.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>GNOME Flashback</td><td>X11</td><td>Metacity or Marco</td><td>Metacity or Marco</td><td><code>MarcoWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Uses the shared Marco/Metacity X11 backend instead of the GNOME Shell extension bridge.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>LXDE</td><td>X11</td><td>Openbox</td><td>compton or picom</td><td><code>OpenboxWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> A compositor is required for complete window previews.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>LXQt</td><td>X11</td><td>Openbox</td><td>compton or picom</td><td><code>OpenboxWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Implemented; real-session verification remains.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>Other EWMH desktops</td><td>X11</td><td>Other EWMH WM</td><td>Any or none</td><td><code>EwmhFallbackWindowBackend</code></td><td>PASS</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> Generic EWMH-compatible fallback.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>XFCE</td><td>Wayland</td><td>N/A</td><td>Varies</td><td>None</td><td>NOT IMPLEMENTED</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> No supported Wayland integration backend.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>Sway</td><td>Wayland</td><td>N/A</td><td>Sway / wlroots</td><td>None</td><td>NOT IMPLEMENTED</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> No supported Wayland integration backend.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>Hyprland</td><td>Wayland</td><td>N/A</td><td>Hyprland</td><td>None</td><td>NOT IMPLEMENTED</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> No supported Wayland integration backend.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>Wayfire</td><td>Wayland</td><td>N/A</td><td>Wayfire / wlroots</td><td>None</td><td>NOT IMPLEMENTED</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> No supported Wayland integration backend.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>labwc</td><td>Wayland</td><td>N/A</td><td>labwc / wlroots</td><td>None</td><td>NOT IMPLEMENTED</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> No supported Wayland integration backend.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>COSMIC</td><td>Wayland</td><td>N/A</td><td>COSMIC compositor</td><td>None</td><td>NOT IMPLEMENTED</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> No supported Wayland integration backend.</td></tr>
    <tr><td colspan="6"><hr></td></tr>
    <tr><td>Generic or other desktop</td><td>Wayland</td><td>N/A</td><td>Other Wayland compositor</td><td>None</td><td>NOT IMPLEMENTED</td></tr>
    <tr><td colspan="6"><strong>Comments:</strong> No supported Wayland integration backend.</td></tr>
  </tbody>
</table>

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
Wayland. **On unsupported desktop sessions, the dock can still start, but
desktop-specific window actions and previews may not be available.**


## Installation

### Debian and Ubuntu based systems

Install the build and runtime dependencies:

```sh
git clone https://github.com/yoosamui/docklight6.git
cd docklight6
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
On the next login, DockLight will start automatically.

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
# executable: build-debug/src/docklight6

./build.sh release --clean
# executable: build-release/src/docklight6
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
On GNOME Wayland, **Autohide Effect** offers a `GNOME` compositor effect which
keeps the dock at its edge while scaling the complete dock around its centre
and fading, matching Plasma Wayland's map/unmap behavior. `Slide and Fade`
instead combines outward movement with opacity. The GNOME Shell extension
owns both effects for native Wayland and XWayland presentation.
On Plasma Wayland, **Autohide Effect** offers the existing `Plasma` behavior
and the KDE-specific movement-only `Slide` behavior. KWin owns DockLight's
screen-edge reveal activation, so an overlapping Plasma panel cannot cover the
trigger and a newly mapped GTK edge strip cannot reverse a pending hide. At a
boundary shared by two monitors, KWin detects crossing that boundary along the
dock instead of activating at the adjacent monitor's outer edge.

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
