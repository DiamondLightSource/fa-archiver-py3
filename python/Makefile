# Install files in global directory
TOP = ..
-include $(TOP)/Makefile.private

# Install a lightly edited wrapper to run fa-viewer.
FA_SUBST = \
    's:^PYTHON=.*$$:PYTHON='\'$(PYTHON)\'':; \
     s:^HERE=.*$$:HERE='\'$(CURDIR)\'':'

all:

install:
	[ -n '$(SCRIPT_DIR)' -a -n '$(PYTHON)' ] || { \
            echo >&2 Must define SCRIPT_DIR and PYTHON; false; }
	sed <fa-viewer >$(SCRIPT_DIR)/fa-viewer $(FA_SUBST)
	chmod +x $(SCRIPT_DIR)/fa-viewer

.PHONY: install
