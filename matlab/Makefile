# Make file to install files in global matlab directory

TOP = ..
MATLAB_DIR = $(PREFIX)/matlab
-include $(TOP)/Makefile.private

MATLAB_FILES = fa_zoomer.m fa_load.m fa_id2name.m fa_name2id.m

# Nothing to be done by default
all:

install:
	install -m444 $(MATLAB_FILES) $(MATLAB_DIR)

.PHONY: install
