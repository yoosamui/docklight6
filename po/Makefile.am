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
