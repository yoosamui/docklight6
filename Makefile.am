SUBDIRS = src po

EXTRA_DIST = \
    SETUP.md \
    autogen.sh \
    build.sh \
    createpo.sh \
    install_dependencies.sh \
    setup_backend.sh \
    setup_presentation.sh \
    validate_presentation.sh \
    tests/build_script_test.sh \
    tests/setup_backend_test.sh \
    tests/setup_presentation_test.sh \
    tests/validate_presentation_test.sh \
    gnome/README.md \
    gnome/install-window-integration.sh \
    gnome/docklight-window-integration@docklight6/metadata.json \
    gnome/docklight-window-integration@docklight6/extension.js \
    gnome/docklight-window-integration@docklight6/placement.js \
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
    protocols/ext-foreign-toplevel-list-v1.xml \
    protocols/ext-image-capture-source-v1.xml \
    protocols/ext-image-copy-capture-v1.xml \
    protocols/zkde-screencast-unstable-v1.xml \
    data/images/README.md \
    $(docklight_session_images)

TESTS = \
    tests/build_script_test.sh \
    tests/setup_backend_test.sh \
    tests/setup_presentation_test.sh \
    tests/validate_presentation_test.sh

dist_pkgdata_DATA = \
    plasma/geometry-bridge/ensure-geometry-bridge.js

docklighticonsdir = $(pkgdatadir)/icons

applicationsdir = $(datadir)/applications

dist_applications_DATA = \
    data/org.docklight6.desktop

dist_docklighticons_DATA = \
    data/icons/128x128/docklight.home.png

# The Session dialog's icon selector reads whatever is present in data/images,
# so the directory is enumerated at build time instead of being listed file by
# file. Installing through a hook keeps adding an image a pure data change.
docklightimagesdir = $(pkgdatadir)/images

docklight_session_images = \
    $(wildcard $(srcdir)/data/images/*.png) \
    $(wildcard $(srcdir)/data/images/*.svg) \
    $(wildcard $(srcdir)/data/images/*.jpg) \
    $(wildcard $(srcdir)/data/images/*.jpeg) \
    $(wildcard $(srcdir)/data/images/*.bmp) \
    $(wildcard $(srcdir)/data/images/*.xpm)

# The selector shows every file in the installed directory, so installing has
# to mirror data/images exactly. Copying without pruning would leave an image
# that was renamed or deleted in the source tree visible forever. This
# directory belongs entirely to Docklight, so removing what is no longer part
# of the set is safe.
install-data-local:
	$(MKDIR_P) "$(DESTDIR)$(docklightimagesdir)"
	@current=''; \
	for image in $(docklight_session_images); do \
	    current="$$current `echo "$$image" | sed 's|.*/||'` "; \
	done; \
	for installed in "$(DESTDIR)$(docklightimagesdir)"/*; do \
	    test -f "$$installed" || continue; \
	    name=`echo "$$installed" | sed 's|.*/||'`; \
	    case "$$current" in \
	    *" $$name "*) ;; \
	    *) echo " rm -f $$installed"; rm -f "$$installed" || exit 1;; \
	    esac; \
	done; \
	for image in $(docklight_session_images); do \
	    test -f "$$image" || continue; \
	    echo " $(INSTALL_DATA) $$image $(DESTDIR)$(docklightimagesdir)"; \
	    $(INSTALL_DATA) "$$image" "$(DESTDIR)$(docklightimagesdir)" || exit 1; \
	done

uninstall-local:
	@for image in $(docklight_session_images); do \
	    name=`echo "$$image" | sed 's|.*/||'`; \
	    rm -f "$(DESTDIR)$(docklightimagesdir)/$$name"; \
	done
	-rmdir "$(DESTDIR)$(docklightimagesdir)" 2>/dev/null || true

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
