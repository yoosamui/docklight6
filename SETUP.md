# DockLight Setup and Development Guide

This guide covers dependencies, building, testing, installation, desktop
integrations, translations, cleanup, and the scripts in this repository. Run
commands from the repository root unless noted otherwise.

## Requirements

DockLight is a C++17 GTK 3 application built with Autotools. The core build
requires:

- a C/C++ compiler, `make`, and `pkg-config`;
- Autoconf, Automake, Libtool, and Autopoint;
- gettext tools (`msgfmt`, `xgettext`, and `msgmerge`);
- gtkmm 3.24 or newer and GTK Layer Shell;
- PipeWire 0.3 and Wayland client development files;
- `wayland-scanner`;
- libwnck 3, GDK X11, Xlib, XComposite, and XRender development files.

On Debian or Ubuntu, install the repository's dependency set with:

```sh
sudo ./install_dependencies.sh
```

Optional workflows need extra tools:

```sh
# Debugging with 1.debugbuild.sh
sudo apt install gdb

# JavaScript tests, if Node.js is not already installed
sudo apt install nodejs

# Plasma geometry bridge
sudo apt install qt6-base-dev qt6-declarative-dev

# XWayland presentation diagnostics
sudo apt install wmctrl x11-utils
```

GNOME integration requires `gnome-extensions`. Plasma integration requires
Plasma 6 tools including `kpackagetool6`, `kwriteconfig6`,
`kbuildsycoca6`, and `qdbus6` (or `qdbus` for the basic integration).

## Build directories

Builds must be out of source. Scripts recognize `DOCKLIGHT_BUILD_DIR`; a
relative value is resolved from the repository root.

| Directory | Purpose |
| --- | --- |
| `build/` | Default used by `autogen.sh` and `install_docklight.sh` |
| `build-debug/` | Debug helper build |
| `build-release/` | Release helper build |

Never point `DOCKLIGHT_BUILD_DIR` at the source directory or `/`.
`clean.sh` rejects both unsafe locations.

## Standard development build

For a debug-friendly build that does not install or launch anything:

```sh
./autogen.sh
make -C build -j"$(nproc)"
make -C build check
./build/src/docklight6
```

`autogen.sh` regenerates Autotools files, creates the build directory, and
runs `configure`. Its defaults are `-O0 -g3` for C and
`-O0 -g3 -DDEBUG` for C++. Caller-provided `CFLAGS` and `CXXFLAGS` take
precedence. Arguments are forwarded to `configure`:

```sh
DOCKLIGHT_BUILD_DIR=build-local \
    ./autogen.sh --prefix="$HOME/.local"
make -C build-local -j"$(nproc)"
```

After changing source files, rerun `make`. Rerun `autogen.sh` after changing
`configure.ac`, a `Makefile.am`, or another build-system input.

## Debug builds

### Build without installing

```sh
DOCKLIGHT_BUILD_DIR=build-debug \
    CFLAGS="-O0 -g3" \
    CXXFLAGS="-O0 -g3 -DDEBUG" \
    ./autogen.sh
make -C build-debug -j"$(nproc)"
make -C build-debug check
./build-debug/src/docklight6
```

### Build, install, and run

```sh
./build_debug.sh
```

This helper removes `build-debug/`, configures and builds it, installs the
binary as `/usr/local/bin/docklight6` using `sudo`, stops a running
DockLight process, and launches the installed binary. Its defaults can be
overridden with `DOCKLIGHT_BUILD_DIR`, `DEBUG_CFLAGS`, and
`DEBUG_CXXFLAGS`.

The script ends with `exec`, so it remains attached to the launched
application.

### Build and launch under GDB

```sh
./1.debugbuild.sh
```

This interactive developer shortcut performs a clean debug build, installs the
binary into `/usr/local/bin`, stops an existing DockLight process, and runs
`gdb /usr/local/bin/docklight6`. Enter `run` at the GDB prompt.

## Release builds

### Build without installing

```sh
DOCKLIGHT_BUILD_DIR=build-release \
    CFLAGS="-O2 -DNDEBUG" \
    CXXFLAGS="-O2 -DNDEBUG" \
    ./autogen.sh
make -C build-release -j"$(nproc)"
make -C build-release check
```

The binary is `build-release/src/docklight6`.

### Build, install, and run

```sh
./build_release.sh
```

This helper removes and recreates `build-release/`, builds with
`-O2 -DNDEBUG`, installs `/usr/local/bin/docklight6`, stops the current
DockLight process, and launches the new binary. Override its defaults with
`RELEASE_CFLAGS`, `RELEASE_CXXFLAGS`, and `DOCKLIGHT_BUILD_DIR`.

### Incremental release shortcut

```sh
./2.releasebuild.sh
```

This helper defaults to `build/`. It reconfigures only when the build is
missing or has different flags, then builds, installs, stops the running
application, and launches the installed binary.

On KDE Plasma Wayland, the installed path is significant. KWin authorizes
restricted screenshot and screencast interfaces using the executable path in
`data/org.docklight6.desktop`. A build-tree binary may show icons instead of
live thumbnails because it is a different, unregistered executable.

## Tests and checks

Run the complete suite from a configured build directory:

```sh
make -C build-debug check
```

It includes C++ tests, GNOME and KWin JavaScript tests, Plasma geometry tests,
backend setup tests, and presentation script tests. Detailed failures are
written to `test-suite.log` files below the build directory.

Useful pre-commit checks:

```sh
make -C build-debug -j"$(nproc)"
make -C build-debug check
git diff --check
```

Run a compiled test directly:

```sh
./build-debug/src/window_registry_test
```

Run one Automake test with its normal environment:

```sh
make -C build-debug/src check TESTS=tests/gnome_placement_test.js
```

## Installing DockLight

### Automated core installation

```sh
sudo ./install_docklight.sh
```

This configures `build/`, builds the project, runs `make install`, and
deploys translations under `/usr/local`. It installs the core only. Return to
the logged-in desktop user afterward and configure integration without
`sudo`:

```sh
./setup_backend.sh auto
```

### Manual installation

```sh
make -C build -j"$(nproc)"
sudo make -C build install
```

The default prefix is `/usr/local`. To stage a package without changing the
live system:

```sh
make -C build DESTDIR=/tmp/docklight-package install
```

## Desktop backend setup

Run backend setup as the logged-in desktop user, never with `sudo`:

```sh
./setup_backend.sh auto
```

Available commands:

```sh
./setup_backend.sh gnome
./setup_backend.sh plasma
./setup_backend.sh x11
./setup_backend.sh status
./setup_backend.sh status gnome
./setup_backend.sh status plasma
./setup_backend.sh status x11
```

Integrations may coexist for users who log into multiple desktop sessions.

### GNOME Wayland

```sh
./setup_backend.sh gnome
```

This packages, installs, and enables
`docklight-window-integration@docklight6`. Log out and back in after a new
installation or update so GNOME Shell loads it. If necessary:

```sh
gnome-extensions enable docklight-window-integration@docklight6
```

The equivalent configured-build target is:

```sh
make -C build install-gnome-integration
```

See `gnome/README.md` for GNOME-specific architecture and presentation notes.

### Plasma Wayland

Install the required KWin script:

```sh
./setup_backend.sh plasma
```

Install optional accurate minimize geometry and the custom effect:

```sh
./setup_backend.sh plasma \
    --with-geometry-bridge \
    --with-minimize-effect
```

Individual configured-build targets are:

```sh
make -C build install-kwin-integration
make -C build install-kwin-minimize-effect
make -C build install-plasma-geometry-bridge
```

These are per-user installations and must not run with `sudo`. The geometry
bridge can instead be installed system-wide:

```sh
sudo make -C build install-plasma-geometry-bridge-system
```

See `plasma/geometry-bridge/README.md` for its requirements and behavior.

### X11

```sh
./setup_backend.sh x11
```

No companion package is installed; DockLight uses EWMH directly.

## Native and XWayland presentation

Presentation controls how DockLight's GTK windows appear and is independent of
the window-integration backend:

```sh
./setup_presentation.sh native
./setup_presentation.sh xwayland
./setup_presentation.sh status
```

The per-user choice is stored in
`${XDG_CONFIG_HOME:-$HOME/.config}/docklight6/presentation.conf`. Restart
DockLight after changing it.

Test XWayland for one run without changing the saved setting:

```sh
./run_xwayland_prototype.sh
```

The launcher selects a binary in this order:

1. `DOCKLIGHT_PROTOTYPE_BINARY`, if set;
2. `build-debug/src/docklight6`;
3. `build/src/docklight6`;
4. `/usr/local/bin/docklight6`.

Close a running DockLight instance first. Test automation can bypass the guard
with `DOCKLIGHT_PROTOTYPE_SKIP_RUNNING_CHECK=1`.

Validate a running GNOME/XWayland instance and its EWMH properties:

```sh
./validate_presentation.sh
```

## Translation workflow

Languages are listed in `po/LINGUAS`; source inputs are in
`po/POTFILES.in`.

Update the template and merge it into every catalog:

```sh
make -C build-debug/po update-pot
make -C build-debug/po update-po
```

Validate one or all catalogs:

```sh
msgfmt --check --check-format -o /dev/null po/de.po

for file in po/*.po; do
    msgfmt --check --check-format -o /dev/null "$file"
done
```

Translation scripts:

- `createpo.sh` creates missing catalogs for languages in `po/LINGUAS`; it
  does not replace existing `.po` files.
- `po/compile_all.sh` compiles catalogs into language directories below
  `po/`; it removes and recreates those generated directories.
- `po/deploymo.sh` installs all catalogs in
  `/usr/local/share/locale` and must run as root.
- `po/merge.sh <language>` is a legacy single-language
  `intltool-update` workflow. Prefer the maintained `update-pot` and
  `update-po` make targets.

## Cleaning and rebuilding

Remove the selected build directory:

```sh
./clean.sh
DOCKLIGHT_BUILD_DIR=build-debug ./clean.sh
```

The script deletes the entire selected directory. It does not remove installed
files or user configuration. For incremental cleanup that keeps the configured
tree:

```sh
make -C build-debug clean
```

## Script reference

| Script | Purpose | Side effect |
| --- | --- | --- |
| `autogen.sh` | Regenerate and configure an out-of-source build | Build files |
| `build_debug.sh` | Clean debug build, install, stop old process, run | System/session |
| `build_release.sh` | Clean release build, install, stop old process, run | System/session |
| `1.debugbuild.sh` | Clean debug build, install, launch under GDB | System/session |
| `2.releasebuild.sh` | Incremental release build, install, run | System/session |
| `clean.sh` | Delete the selected build directory | Deletes build output |
| `install_dependencies.sh` | Install Debian/Ubuntu dependencies | System packages |
| `install_docklight.sh` | Build and install core and translations | `/usr/local` |
| `setup_backend.sh` | Install or inspect desktop integration | Per-user |
| `setup_presentation.sh` | Save native/XWayland presentation | Per-user config |
| `run_xwayland_prototype.sh` | Run one XWayland test instance | Process only |
| `validate_presentation.sh` | Diagnose XWayland presentation | Read-only |
| `createpo.sh` | Create missing translation catalogs | Source files |
| `po/compile_all.sh` | Compile catalogs below `po/` | Generated files |
| `po/deploymo.sh` | Install compiled translations | `/usr/local` |
| `po/merge.sh` | Legacy one-language catalog merge | Source file |

## Troubleshooting

### Configure cannot find a dependency

Install the corresponding development package reported by `configure`.
Confirm that the main modules are visible:

```sh
pkg-config --modversion \
    gtkmm-3.0 \
    gtk-layer-shell-0 \
    libpipewire-0.3 \
    wayland-client \
    libwnck-3.0
```

Also confirm that `msgfmt`, `xgettext`, `msgmerge`, and
`wayland-scanner` are on `PATH`.

### The wrong build flags are still in use

Autotools caches configuration. Remove and recreate the relevant directory:

```sh
DOCKLIGHT_BUILD_DIR=build-debug ./clean.sh
```

### GNOME integration does not activate

Run `./setup_backend.sh status gnome`. After installing or updating the
extension, log out and back in.

### Plasma previews show icons instead of thumbnails

Install and run `/usr/local/bin/docklight6` so its path matches KDE's desktop
entry authorization, then refresh integration:

```sh
./setup_backend.sh plasma
```

### XWayland validation fails

Confirm that the mode is `xwayland`, `WAYLAND_DISPLAY` and `DISPLAY` are
available, DockLight is running, and the diagnostic tools are installed:

```sh
./setup_presentation.sh status
./validate_presentation.sh
```

### Find detailed test failures

```sh
find build-debug -name test-suite.log -print
```

The logs contain the failing test command and its output.

