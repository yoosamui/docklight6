# docklight6 with x11 and wayland support.

**Project Status:** Current Version: `6.0.27`

> This project is actively under development. Features, APIs, configuration parameters, and documentation may change between releases.

## Building

Docklight uses an out-of-source build directory so generated files and
compiled objects do not modify `src/`:

```sh
./autogen.sh
make -C build -j"$(nproc)"
```

The resulting executable is `build/src/docklight6`. To start over with a
clean build, run `./clean.sh`, then run `./autogen.sh` again.

For the KDE Wayland development workflow, `./makec.sh` builds in `build/`,
copies the executable to its authorized installed path, and starts it.

## KDE Plasma Wayland window integration

Docklight uses a KWin script to receive window state on KDE Plasma
Wayland. Install or update the script for the current user with:

```sh
make -C build install-kwin-integration
```

The target installs the package with `kpackagetool6`, enables it in
`kwinrc`, and asks KWin to reload its configuration. Root access is not
required.

Start Docklight after installation:

```sh
./build/src/docklight6
```

Successful startup includes both messages:

```text
KWin window integration is ready for the KWin script
KWin window integration connected
```
