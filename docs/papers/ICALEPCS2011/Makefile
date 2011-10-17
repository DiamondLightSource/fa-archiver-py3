DOCUMENT = THCHMUST03

DEPENDS += THCHMUST03f1.tikz
DEPENDS += THCHMUST03f2.tikz
DEPENDS += THCHMUST03f3.tikz
DEPENDS += THCHMUST03f4.tikz
DEPENDS += THCHMUST03f5.tikz

FIGURES += THCHMUST03f6.png


default: $(DOCUMENT).pdf

$(DOCUMENT).pdf: $(DEPENDS) $(FIGURES)

CITE_GREP = There were undefined references.
REF_GREP = Rerun to get cross-references right.
NOFILE_GREP = ^No file

LATEX = pdflatex

%.pdf: %.tex
	$(LATEX) </dev/null -halt-on-error $*
	if grep -q '$(CITE_GREP)\|$(REF_GREP)\|$(NOFILE_GREP)' $*.log; then \
            $(LATEX) </dev/null -halt-on-error $*; \
        fi
	rm -f $*.log

clean:
	rm -f $(DOCUMENT).aux

clean-all: clean
	rm -f $(DOCUMENT).pdf

.PHONY: default clean clean-all
