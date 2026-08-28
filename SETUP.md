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
# Debugging with ./build.sh debug --gdb
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
| `build/` | Low-level default used by `autogen.sh` |
| `build-debug/` | Debug helper build |
| `build-release/` | Release helper build |

Never point `DOCKLIGHT_BUILD_DIR` at the source directory or `/`.
`build.sh` and `autogen.sh` reject unsafe locations.

## Standard development build

The normal developer entry point creates a debug build without installing or
launching anything:

```sh
./build.sh debug
./build-debug/src/docklight6
```

Add `--check` to run the complete test suite:

```sh
./build.sh debug --check
```

`build.sh` selects the correct directory and compiler flags, configures it when
needed, and performs an incremental parallel build. Run `./build.sh --help`
for its complete command-line reference.

## Unified build command

The general syntax is:

```sh
./build.sh <debug|release> [OPTIONS]
```

Modes map to these defaults:

| Mode | Directory | C/C++ flags |
| --- | --- | --- |
| `debug` | `build-debug/` | `-O0 -g3`; C++ also uses `-DDEBUG` |
| `release` | `build-release/` | `-O2 -DNDEBUG` |

Common workflows:

```sh
# Incremental builds
./build.sh debug
./build.sh release

# Clean debug build with complete tests; clean release build
./build.sh debug --clean --check
./build.sh release --clean

# Install the complete release under /usr/local
./build.sh release --install

# Install, stop an old instance, and run
./build.sh release --install --restart --run

# Debug in GDB without installing
./build.sh debug --gdb

# Install the debug build and debug the registered executable
./build.sh debug --install --restart --gdb
```

Options:

- `--clean` removes the mode's build directory before rebuilding.
- `--clean-only` removes the build directory and exits without rebuilding.
- `--check` runs `make check` after the build. Use it with debug mode for full
  validation because release mode defines `NDEBUG`, disabling C++ `assert()`
  checks.
- `--install` runs the complete `make install` workflow and requests `sudo`
  when the caller is not root.
- `--run` launches the resulting executable.
- `--gdb` launches it under GDB and cannot be combined with `--run`.
- `--restart` stops a running `docklight6` before `--run` or `--gdb`.
- `--jobs N` overrides the default `nproc` parallelism.

When `--run` or `--gdb` is used from a non-Wayland session, the build script
launches DockLight with `--presentation=native`. This keeps an XWayland mode
saved for a Wayland desktop from preventing a launch after switching to an
X11 desktop such as XFCE. The saved per-user presentation setting is not
modified.

The default operation is deliberately non-invasive: it builds only. It never
installs, stops, or launches DockLight unless the corresponding option is
present.

Override mode defaults with these environment variables:

```sh
DOCKLIGHT_BUILD_DIR=build-custom ./build.sh debug
DEBUG_CFLAGS="-Og -g3" DEBUG_CXXFLAGS="-Og -g3 -DDEBUG" \
    ./build.sh debug --clean
RELEASE_CFLAGS="-O3 -DNDEBUG" RELEASE_CXXFLAGS="-O3 -DNDEBUG" \
    ./build.sh release --clean
```

When `--install` is present, `--run` and `--gdb` use
`/usr/local/bin/docklight6`. Override that path with
`DOCKLIGHT_INSTALLED_BINARY` if the project was configured with another
prefix.

### Low-level Autotools workflow

Use `autogen.sh` directly when passing custom `configure` arguments or when a
mode-specific build is not appropriate:

```sh
DOCKLIGHT_BUILD_DIR=build-local \
    ./autogen.sh --prefix="$HOME/.local"
make -C build-local -j"$(nproc)"
make -C build-local check
```

`autogen.sh` regenerates Autotools files and uses debug-friendly flags unless
the caller supplies `CFLAGS` and `CXXFLAGS`. Arguments are forwarded to
`configure`.

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
./build.sh debug --clean --check
./build.sh release --clean --install
```

This validates the debug build, then creates and installs the release binary,
desktop file, icons, data, and translations under `/usr/local`. The script
requests `sudo` only for `make install`. It installs the core only; configure
desktop integration afterward as the logged-in user:

```sh
./setup_backend.sh auto
```

### Manual installation

```sh
./build.sh release
sudo make -C build-release install
```

The default prefix is `/usr/local`. To stage a package without changing the
live system:

```sh
make -C build-release DESTDIR=/tmp/docklight-package install
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

### GNOME Shell (Wayland and X11)

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

The GNOME Wayland settings dialog offers two compositor-owned autohide
effects:

- `GNOME` matches the Plasma Wayland effect: the dock stays fixed at its edge
  while the complete dock scales around its centre and fades.
- `Slide and Fade` moves the dock outward while fading it over 200 ms.

The corresponding configuration values are `gnome` and `slide_fade`.

On GNOME Shell X11, `GNOME` delegates the same centred scale-and-fade drawing
to the extension while retaining native EWMH/XComposite window handling and
the GTK edge trigger. `Plasma` and `Slide` remain native X11 alternatives. If
the extension is absent or cannot identify the main dock, DockLight stays on
the existing native animation path.

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

The Plasma Wayland settings dialog offers two autohide effects:

- `Plasma` keeps the existing layer-surface map/unmap effect.
- `Slide` uses a smooth client-rendered outward movement without fading. Its
  fully clipped layer surface remains mapped while hidden so KWin does not add
  a second animation during reveal.

The corresponding configuration values are `plasma` and `slide`. Existing
Plasma Wayland `slide_fade` settings are interpreted as `slide`.

The KWin integration owns the Plasma Wayland screen-edge reveal trigger.
DockLight does not map its GTK reveal strip in this mode, preventing both
input conflicts with an overlapping Plasma panel and immediate hide/reveal
reversal beneath a stationary edge pointer. If the configured dock edge is an
internal boundary between monitors, the KWin script uses an actual pointer
crossing within the dock span; the far edge of the adjacent monitor is ignored.

### X11

```sh
./setup_backend.sh x11
```

No companion package is installed; DockLight uses EWMH directly.

## Native and XWayland presentation

Presentation controls how DockLight's GTK windows appear and is independent of
the window-integration backend:

```sh
./setup_presentation.sh auto
./setup_presentation.sh native
./setup_presentation.sh xwayland
./setup_presentation.sh status
```

`auto` is the default and recommended mode. It selects XWayland on GNOME
Wayland when an XWayland display is available, and selects native presentation
on Plasma Wayland, X11, and other sessions. `native` and `xwayland` remain
explicit overrides for testing or compatibility.

The per-user choice is stored as `mode=auto`, `mode=native`, or
`mode=xwayland` in
`${XDG_CONFIG_HOME:-$HOME/.config}/docklight6/presentation.conf`. Restart
DockLight after changing it.

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
./build.sh debug --clean-only
./build.sh release --clean-only
DOCKLIGHT_BUILD_DIR=build-custom ./build.sh debug --clean-only
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
| `build.sh` | Build, test, clean, install, run, or debug | Depends on options |
| `install_dependencies.sh` | Install Debian/Ubuntu dependencies | System packages |
| `setup_backend.sh` | Install or inspect desktop integration | Per-user |
| `setup_presentation.sh` | Save automatic/native/XWayland presentation | Per-user config |
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
./build.sh debug --clean-only
./build.sh debug
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
