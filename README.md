


# DockLight 6


<img width="127" height="127" alt="docklight home" src="https://github.com/user-attachments/assets/7e338d8e-2ce2-4ede-b8dc-48aed38e25d2" />

DockLight is a Generic lightweight application dock for Linux with X11 and Wayland
support. It provides application launchers, window management, live window
previews, multi-monitor placement, bookmarks (sessions) and configurable auto-hide behavior.

**Current version:** `6.0.40`


**Project status:** Feature complete. Development now focuses on maintenance,
bug fixes, compatibility, and translations.

DockLight's complete window-management integration is designed for X11 or
Wayland. **On unsupported desktop sessions, the dock can still start, but
desktop-specific window actions and previews may not be available.**


**Demo video:**
https://youtu.be/ZBSN9LmiqUY


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

## Uninstallation

Close DockLight before removing it. If it is running, use **Exit** from the
Home menu or run:

```sh
gapplication action org.docklight6 exit
```

From the source directory used for installation, remove the installed files
with:

```sh
sudo make -C build-release uninstall
```

Use the same build directory and installation prefix you originally installed
with. If you installed a debug build, replace `build-release` with
`build-debug`. Keep that configured build directory until removal is complete.

Desktop integrations are installed separately and are not removed by
`make uninstall`. Run the commands for your desktop as your normal user,
without `sudo`.

For GNOME, disable and remove the Shell extension:

```sh
gnome-extensions disable docklight-window-integration@docklight6
gnome-extensions uninstall docklight-window-integration@docklight6
```

For KDE Plasma, disable the KWin script and remove its package:

```sh
kwriteconfig6 --file kwinrc --group Plugins \
    --key org.docklight6.windowintegrationEnabled false
kpackagetool6 --type KWin/Script --remove org.docklight6.windowintegration
rm -f "${XDG_DATA_HOME:-$HOME/.local/share}/applications/org.docklight6.desktop"
kbuildsycoca6 --noincremental
```

If you installed the optional Plasma minimize effect or geometry bridge, remove
those components separately as well. Log out and back in after removing desktop
integrations so the session unloads them. X11 needs no companion removal unless
you installed the optional GNOME Shell integration.

Remove any DockLight entry from your desktop's autostart settings and any custom
keyboard shortcuts you created. Your settings remain in
`~/.config/docklight6/`; delete that folder only if you also want to discard your
saved configuration, launchers, and sessions. If you use a custom
`XDG_CONFIG_HOME`, look for the `docklight6` folder there instead.

## Development builds

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

## Desktop command shortcuts

Turning off **Display Home Icon** in Settings shows a **Desktop Shortcuts**
notice with the commands below. Its text can be selected and copied.

These actions remain available when the Home icon is hidden. With the updated
DockLight running, run a command or assign it to a custom keyboard shortcut
in your desktop's keyboard settings:

```sh
gapplication action org.docklight6 settings
gapplication action org.docklight6 session
gapplication action org.docklight6 about
gapplication action org.docklight6 exit
```

`settings`, `session` (singular), and `about` open the corresponding dialogs.
`exit` closes DockLight, dismissing any open dialogs without saving pending
Session edits. Repeated requests for an already open dialog do not create
additional copies.

For example, assign Ctrl+I to Settings. The desktop handles the key globally
and sends the action to DockLight over the user session's D-Bus. DockLight does
not need keyboard focus or a visible Home icon.

`gapplication` is a GLib command-line tool (`libglib2.0-bin` on Debian/Ubuntu).
The command requires a running updated DockLight in the same user session; it
does not start DockLight. The shortcut is configured by the desktop, not by a
`[keybindings]` section in `docklight.conf`. Choose an unused combination to
avoid replacing an existing desktop or application shortcut.

For standalone Openbox, add this inside the existing `<keyboard>` section of
`~/.config/openbox/rc.xml` (edit the existing binding if Ctrl+I is already used):

```xml
<keybind key="C-i">
  <action name="Execute">
    <command>gapplication action org.docklight6 settings</command>
  </action>
</keybind>
```

Then run `openbox --reconfigure`. Openbox handles the key itself; no compositor
such as picom is required. For Wayland, assign the same command through the
desktop/compositor's shortcut settings. Command dispatch is independent of
DockLight's window backend; the desktop controls focus and dialog placement.

## Configuration and diagnostics

### Settings and hover effects

Use the **Settings** dialog to configure DockLight. The application creates
`~/.config/docklight6/docklight.conf` automatically and watches it for changes.

The default hover effect is **Magnified**. DockLight uses this default when
`hover_effect` is missing or empty, while preserving any effect you explicitly
select.

### Autohide on GNOME Wayland

In Settings, **Autohide Effect → GNOME** keeps the dock at its screen edge
while scaling it around its centre and fading it in or out. This matches the
map/unmap behavior on Plasma Wayland.

Choose **Slide and Fade** to combine outward movement with fading instead.
The GNOME Shell extension handles both effects, whether DockLight uses native
Wayland or XWayland presentation.

### Autohide on GNOME Shell X11

The **GNOME** and **Plasma** choices both use the Shell extension's centred
scale-and-fade animation. Existing configurations saved with **Plasma** also
receive this behavior. Window discovery and previews continue to use native
X11 EWMH/XComposite integration.

**Slide** uses DockLight's native X11 animation. If the Shell bridge is
unavailable, DockLight automatically uses native effects for every choice.

### Autohide on KDE Plasma Wayland

Choose **Plasma** for the compositor's existing animation, or **Slide** for
the KDE-specific movement-only effect.

KWin handles screen-edge activation to reveal the dock. This lets the trigger
work even when a Plasma panel overlaps it, and prevents a newly mapped GTK
edge strip from interrupting a pending hide.

When the dock sits at a boundary between two monitors, KWin detects pointer
crossings along that boundary rather than using the adjacent monitor's outer
edge.

To hide KWin's blue edge highlight while keeping DockLight's reveal trigger,
disable **Desktop Effects → Appearance → Highlight Screen Edges and Hot
Corners**. See [Disable KWin's blue screen-edge indication](SETUP.md#disable-kwins-blue-screen-edge-indication)
for the command-line equivalent and instructions to restore the highlight.

### Monitor and integration diagnostics

To find monitor names for monitor-specific placement, run:

```sh
docklight6 --list-monitors
```

To inspect the installed, enabled, and active state of desktop integrations
without changing your system, run this from the source directory:

```sh
./setup_backend.sh status
```

On KDE Plasma Wayland, a successful integration startup includes these log
messages:

```text
KWin window integration is ready for the KWin script
KWin window integration connected
```

## Contributing

Ideas, feedback, and pull requests are welcome. If you have a suggestion or
would like to propose a change, [open an issue](https://github.com/yoosamui/docklight6/issues)
and describe what you would like to improve and how it would help users.
You do not need to write code to contribute: bug reports, documentation
improvements, and translations are valuable too.

For code changes, submit a pull request explaining the problem, your proposed
solution, and how you tested it. Please discuss larger changes in an issue
first so we can agree on the direction before you start. See
[SETUP.md](SETUP.md) for build and test instructions, and follow the project's
[architecture guidelines](docs/ARCHITECTURE.md).

## License

See [LICENSE](LICENSE).
