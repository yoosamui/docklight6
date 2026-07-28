SUBDIRS = src

EXTRA_DIST = \
    kwin/install-minimize-effect.sh \
    kwin/install-window-integration.sh \
    kwin/org.docklight6.minimize/metadata.json \
    kwin/org.docklight6.minimize/contents/code/main.js \
    kwin/org.docklight6.minimize/contents/config/main.xml \
    kwin/org.docklight6.windowintegration/metadata.json \
    kwin/org.docklight6.windowintegration/contents/code/main.js \
    plasma/geometry-bridge/README.md \
    plasma/geometry-bridge/install-geometry-bridge.sh \
    plasma/geometry-bridge/plugin/CMakeLists.txt \
    plasma/geometry-bridge/plugin/geometry_bridge.h \
    plasma/geometry-bridge/plugin/geometry_bridge.cpp \
    plasma/geometry-bridge/plugin/geometry_bridge_plugin.cpp \
    plasma/geometry-bridge/package/metadata.json \
    plasma/geometry-bridge/package/contents/ui/main.qml \
    plasma/geometry-bridge/package/contents/ui/bridge/qmldir

.PHONY: \
    install-kwin-integration \
    install-kwin-minimize-effect \
    install-plasma-geometry-bridge

install-kwin-integration:
	$(SHELL) $(srcdir)/kwin/install-window-integration.sh

install-kwin-minimize-effect:
	$(SHELL) $(srcdir)/kwin/install-minimize-effect.sh

install-plasma-geometry-bridge:
	$(SHELL) $(srcdir)/plasma/geometry-bridge/install-geometry-bridge.sh
