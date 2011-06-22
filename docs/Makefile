ifndef TOP
$(error Do not call this make file directly)
endif

MAN_DIR ?= $(PREFIX)/man
DOCS_DIR ?= $(PREFIX)/doc/fa-archiver

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

HTMLDOCS = index.html $(MANPAGES:=.html)

docs: $(MANPAGES:=.man) $(HTMLDOCS)

install: docs
	$(TOP)/docs/install_man $(CURDIR) $(MAN_DIR) $(MANPAGES)
	install -d $(DOCS_DIR)
	install -m444 $(HTMLDOCS) $(DOCS_DIR)

.PHONY: default install docs
