SUBDIRS = src po

EXTRA_DIST = \
    gnome/README.md \
    gnome/install-window-integration.sh \
    gnome/docklight-window-integration@docklight6/metadata.json \
    gnome/docklight-window-integration@docklight6/extension.js \
    kwin/install-minimize-effect.sh \
    kwin/install-window-integration.sh \
    kwin/org.docklight6.minimize/metadata.json \
    kwin/org.docklight6.minimize/contents/code/main.js \
    kwin/org.docklight6.minimize/contents/config/main.xml \
    kwin/org.docklight6.windowintegration/metadata.json \
    kwin/org.docklight6.windowintegration/contents/code/main.js \
    plasma/geometry-bridge/README.md \
    plasma/geometry-bridge/ensure-geometry-bridge.js \
    plasma/geometry-bridge/install-geometry-bridge.sh \
    plasma/geometry-bridge/plugin/CMakeLists.txt \
    plasma/geometry-bridge/plugin/geometry_bridge.h \
    plasma/geometry-bridge/plugin/geometry_bridge.cpp \
    plasma/geometry-bridge/plugin/geometry_bridge_plugin.cpp \
    plasma/geometry-bridge/package/metadata.json \
    plasma/geometry-bridge/package/contents/ui/main.qml \
    plasma/geometry-bridge/package/contents/ui/bridge/qmldir \
    protocols/zkde-screencast-unstable-v1.xml

dist_pkgdata_DATA = \
    plasma/geometry-bridge/ensure-geometry-bridge.js

docklighticonsdir = $(pkgdatadir)/icons

applicationsdir = $(datadir)/applications

dist_applications_DATA = \
    data/org.docklight6.desktop

dist_docklighticons_DATA = \
    data/icons/128x128/docklight.home.png

.PHONY: \
    install-gnome-integration \
    install-kwin-integration \
    install-kwin-minimize-effect \
    install-plasma-geometry-bridge \
    install-plasma-geometry-bridge-system

install-gnome-integration:
	$(SHELL) $(srcdir)/gnome/install-window-integration.sh

install-kwin-integration:
	$(SHELL) $(srcdir)/kwin/install-window-integration.sh

install-kwin-minimize-effect:
	$(SHELL) $(srcdir)/kwin/install-minimize-effect.sh

install-plasma-geometry-bridge:
	$(SHELL) $(srcdir)/plasma/geometry-bridge/install-geometry-bridge.sh

install-plasma-geometry-bridge-system:
	$(SHELL) $(srcdir)/plasma/geometry-bridge/install-geometry-bridge.sh --global
