# docklight6 with x11 and wayland support.

**Project Status:** Current Version:`6.0`

> This project is actively under development. Features, APIs, configuration parameters, and documentation may change between releases.

## KDE Plasma Wayland window integration

Docklight uses a KWin script to receive window state on KDE Plasma
Wayland. Install or update the script for the current user with:

```sh
make install-kwin-integration
```

The target installs the package with `kpackagetool6`, enables it in
`kwinrc`, and asks KWin to reload its configuration. Root access is not
required.

Start Docklight after installation:

```sh
./src/docklight6
```

Successful startup includes both messages:

```text
KWin window integration is ready for the KWin script
KWin window integration connected
```
