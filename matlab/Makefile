# Make file to install files in global matlab directory
ifndef TOP
$(error Do not call this make file directly)
endif

MATLAB_DIR ?= $(PREFIX)/matlab
MCC ?= /dls_sw/prod/tools/RHEL5/bin/mcc

# Substitutions for fa_zoomer.sh
MCRROOT ?= /dls_sw/prod/tools/RHEL5/opt/matlab2008b
GLN_ARCH ?= glnxa64
ARCH ?= amd64

define ZOOMER_SUBST
s:@MCRROOT@:$(MCRROOT):; \
s:@GLN_ARCH@:$(GLN_ARCH):; \
s:@ARCH@:$(ARCH):
endef

MATLAB_FILES = $(wildcard *.m) fa_server_names

default: fa_zoomer

fa_zoomer: $(MATLAB_FILES) fa_zoomer.sh
	$(MCC) -I $(srcdir) -a $(srcdir)/fa_server_names -o fa_zoomer_bin -m $@
	sed '$(ZOOMER_SUBST)' $(srcdir)/fa_zoomer.sh >$@
	chmod +x $@_bin $@

install: fa_zoomer install_m_files
	install -d $(SCRIPT_DIR)
	install fa_zoomer fa_zoomer_bin $(SCRIPT_DIR)

install_m_files: $(MATLAB_FILES)
	install -d $(MATLAB_DIR)
	install -m444 $^ $(MATLAB_DIR)

.PHONY: default install install_m_files
