#!/bin/sh
# Docklight 6.0 — isolate the GTK/Shell handoff regression from the desktop.
# A fake backend supplies compositor state; Xvfb supplies only GTK's X surface.
set -eu
command -v xvfb-run >/dev/null 2>&1 || exit 77
fixture=$(mktemp -d /tmp/docklight-handoff.XXXXXX)
trap 'rm -rf "$fixture"' EXIT HUP INT TERM
ulimit -c 0
xvfb-run -a env \
    GDK_BACKEND=x11 XDG_SESSION_TYPE=wayland XDG_CURRENT_DESKTOP=GNOME \
    XDG_CONFIG_HOME="$fixture/config" XDG_CACHE_HOME="$fixture/cache" \
    ./dock_autohide_handoff_test
