# Install files in global directory


# Install a lightly edited wrapper to run fa-viewer.
FA_SUBST = \
    's:^PYTHON=.*$$:PYTHON='\'$(PYTHON)\'':; \
     s:^HERE=.*$$:HERE='\'$(CURDIR)\'':'

default:

clean:
	find -name \*.pyc -exec rm {} \;

install:
ifndef SCRIPT_DIR
	echo >&2 Must define SCRIPT_DIR; false
endif
	$(PYTHON) -m compileall .
	sed <fa-viewer >$(SCRIPT_DIR)/fa-viewer $(FA_SUBST)
	chmod +x $(SCRIPT_DIR)/fa-viewer
	sed <fa-audio >$(SCRIPT_DIR)/fa-audio $(FA_SUBST)
	chmod +x $(SCRIPT_DIR)/fa-audio

.PHONY: all clean install
