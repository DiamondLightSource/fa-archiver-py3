# Install files in global directory


# Install a lightly edited wrapper to run installed programs
FA_SUBST = \
    's:^PYTHON=.*$$:PYTHON='\'$(PYTHON)\'':; \
     s:^HERE=.*$$:HERE='\'$(CURDIR)\'':'

INSTALL_SCRIPTS = fa-viewer fa-audio

default:

clean:
	find -name \*.pyc -exec rm {} \;

install: $(INSTALL_SCRIPTS:%=$(SCRIPT_DIR)/%)
ifndef SCRIPT_DIR
	echo >&2 Must define SCRIPT_DIR; false
endif
	$(PYTHON) -m compileall .

$(SCRIPT_DIR)/%: % $(SCRIPT_DIR) FORCE
	rm -f $@
	sed <$< >$@ $(FA_SUBST)
	chmod 555 $@

$(SCRIPT_DIR):
	install -d $(SCRIPT_DIR)

FORCE:

.PHONY: default clean install
