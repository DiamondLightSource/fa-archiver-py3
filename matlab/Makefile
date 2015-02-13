# Make file to install files in global matlab directory
ifndef TOP
$(error Do not call this make file directly)
endif

MATLAB_DIR ?= $(PREFIX)/matlab

# Substitutions for fa_zoomer.sh
MCRROOT ?= /dls_sw/prod/tools/RHEL6-x86_64/matlab/R2014a/prefix
ARCH ?= glnxa64

MCC ?= $(MCRROOT)/bin/mcc

define ZOOMER_SUBST
s:@MCRROOT@:$(MCRROOT):; \
s:@ARCH@:$(ARCH):
endef

MATLAB_FILES += fa_capture.m        # Wrapper around fa-capture command
MATLAB_FILES += fa_find_server.m    # Connects to specified FA archiver server
MATLAB_FILES += fa_getids.m         # Reads data stream metadata from server
MATLAB_FILES += fa_id2name.m        # Converts FA id to name etc
MATLAB_FILES += fa_load.m           # Reads requested data set from server
MATLAB_FILES += fa_name2id.m        # Converts name to FA id
MATLAB_FILES += fa_zoomer.m         # Interactive tool for exploring stored data
MATLAB_FILES += tcp_connect.m       # Generic Matlab TCP interface
MATLAB_FILES += progress_bar.m      # Progress bar support
MATLAB_FILES += fa_server_names     # Table of server locations and short names

default: fa_zoomer

fa_zoomer: $(MATLAB_FILES) fa_zoomer.sh
	$(MCC) -I $(srcdir) -a $(srcdir)/fa_server_names -o fa_zoomer_bin -m $@
	sed '$(ZOOMER_SUBST)' $(srcdir)/fa_zoomer.sh >$@
	chmod +x $@_bin $@

install: $(MATLAB_FILES) fa_zoomer
	install -d $(MATLAB_DIR)
	install -m444 $(MATLAB_FILES:%=$(srcdir)/%) $(MATLAB_DIR)
	install -d $(SCRIPT_DIR)
	install fa_zoomer fa_zoomer_bin $(SCRIPT_DIR)

.PHONY: default install install_m_files
