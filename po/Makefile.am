LINGUAS = @ALL_LINGUAS@

PO_FILES = $(LINGUAS:%=%.po)
GMO_FILES = $(LINGUAS:%=%.gmo)

EXTRA_DIST = \
    $(PO_FILES) \
    LINGUAS \
    POTFILES.in \
    compile_all.sh \
    deploymo.sh \
    merge.sh

CLEANFILES = $(GMO_FILES)

.PHONY: update-pot update-po

all-local: $(GMO_FILES)

%.gmo: $(srcdir)/%.po
	$(MSGFMT) --check-format --output-file=$@ $<

install-data-local: $(GMO_FILES)
	@for language in $(LINGUAS); do \
	    directory="$(DESTDIR)$(localedir)/$$language/LC_MESSAGES"; \
	    $(MKDIR_P) "$$directory"; \
	    echo " $(INSTALL_DATA) $$language.gmo $$directory/$(GETTEXT_PACKAGE).mo"; \
	    $(INSTALL_DATA) "$$language.gmo" \
	        "$$directory/$(GETTEXT_PACKAGE).mo"; \
	done

uninstall-local:
	@for language in $(LINGUAS); do \
	    file="$(DESTDIR)$(localedir)/$$language/LC_MESSAGES/$(GETTEXT_PACKAGE).mo"; \
	    echo " rm -f $$file"; \
	    rm -f "$$file"; \
	done

check-local: $(GMO_FILES)

update-pot:
	@files=`sed -e '/^#/d' -e '/^$$/d' $(srcdir)/POTFILES.in`; \
	cd $(top_srcdir) && \
	$(XGETTEXT) \
	    --from-code=UTF-8 \
	    --language=C++ \
	    --keyword=_ \
	    --keyword=N_ \
	    --keyword=C_:1c,2 \
	    --keyword=NC_:1c,2 \
	    --keyword=ngettext:1,2 \
	    --package-name=$(PACKAGE_NAME) \
	    --package-version=$(PACKAGE_VERSION) \
	    --msgid-bugs-address=$(PACKAGE_BUGREPORT) \
	    --output=$(abs_builddir)/$(GETTEXT_PACKAGE).pot \
	    $$files

update-po: update-pot
	@for language in $(LINGUAS); do \
	    echo " $(MSGMERGE) --update $(srcdir)/$$language.po $(GETTEXT_PACKAGE).pot"; \
	    $(MSGMERGE) \
	        --backup=none \
	        --update \
	        $(srcdir)/$$language.po \
	        $(GETTEXT_PACKAGE).pot; \
	done
