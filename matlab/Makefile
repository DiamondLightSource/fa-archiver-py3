# Make file to install files in global matlab directory
ifndef TOP
$(error Do not call this make file directly)
endif

MATLAB_DIR ?= $(PREFIX)/matlab
MCC ?= /dls_sw/prod/tools/RHEL5/bin/mcc
MCRROOT ?= /dls_sw/prod/tools/RHEL5/opt/matlab2008b

MATLAB_FILES = fa_zoomer.m fa_load.m fa_id2name.m fa_name2id.m

default: fa_zoomer

fa_zoomer: fa_zoomer.m fa_load.m fa_zoomer.sh
	$(MCC) -I $(srcdir) -o fa_zoomer_bin -m $@
	sed 's:@MCRROOT@:$(MCRROOT):' $(srcdir)/fa_zoomer.sh >$@
	chmod +x $@_bin $@

install: fa_zoomer
	install -m444 $(MATLAB_FILES) $(MATLAB_DIR)
	install fa_zoomer fa_zoomer_bin $(SCRIPT_DIR)

.PHONY: default install
