SUBDIRS = src

EXTRA_DIST = \
    kwin/install-window-integration.sh \
    kwin/org.docklight6.windowintegration/metadata.json \
    kwin/org.docklight6.windowintegration/contents/code/main.js

.PHONY: install-kwin-integration

install-kwin-integration:
	$(SHELL) $(srcdir)/kwin/install-window-integration.sh
