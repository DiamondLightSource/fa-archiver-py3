ifndef TOP
$(error Do not call this make file directly)
endif

MAN_DIR ?= $(PREFIX)/man

RST2HTML = rst2html
RST2MAN = rst2man

%.html: %.rst
	$(RST2HTML) $^ >$@

%.man: %.rst
	$(RST2MAN) $^ >$@


MANPAGES = \
    fa_sniffer fa-prepare fa-archiver fa-capture \
    fa-viewer fa-spectrum fa-audio falib \
    fa_zoomer fa_load

docs: index.html $(MANPAGES:=.man) $(MANPAGES:=.html)

install: $(MANPAGES:=.man)
	$(TOP)/docs/install_man $(CURDIR) $(MAN_DIR) $(MANPAGES)

.PHONY: default install docs
