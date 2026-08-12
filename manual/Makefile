# ---------------------------------------------------------------------------
# Commander Calc Reference Manual
#
#   make          build the PDF               (build/commander-calc-manual.pdf)
#   make pdf      build, then publish it to    (pdf/commander-calc-manual.pdf)
#   make quick    one pass, for checking a change in a hurry
#   make view     build, then open it
#   make check    report warnings and wide lines from the last build
#   make env      show the toolchain, and what is missing
#   make clean    remove build/
#
# From the project root:  make manual / make manualpdf / make manualview
#                         make manualclean
#
# Requires (Arch):
#   sudo pacman -S texlive-basic texlive-latex texlive-latexrecommended \
#                  texlive-latexextra texlive-pictures texlive-fontsrecommended
#
# Nothing else. Every figure in the manual, the logo included, is drawn in
# TikZ -- there is no image file to convert and no external tool to install.
# ---------------------------------------------------------------------------

DOC      := commander-calc-manual
OUT      := build
PUB      := pdf
TEX      := pdflatex
TEXFLAGS := -interaction=nonstopmode -halt-on-error -file-line-error \
            -output-directory=$(OUT)

SOURCES  := $(DOC).tex \
            $(wildcard style/*.sty) $(wildcard style/*.tex) \
            $(wildcard front/*.tex) $(wildcard ch/*.tex) $(wildcard app/*.tex)

# What a build actually needs. Checked before pdflatex runs, because a
# missing package otherwise surfaces sixty lines into a log as a font error
# with no hint of which package to install.
NEEDS    := memoir.cls tikz.sty mathptmx.sty helvet.sty courier.sty \
            enumitem.sty needspace.sty multirow.sty longtable.sty \
            eso-pic.sty microtype.sty hyperref.sty makeidx.sty
NEEDFONT := uhvr8a.pfb

.PHONY: all pdf quick view check clean env preflight

all: $(OUT)/$(DOC).pdf

# --- preflight -------------------------------------------------------------
# One line per thing that is missing, and the pacman command that fixes it.
preflight:
	@miss=""; \
	 command -v $(TEX)    >/dev/null || miss="$$miss\n    pdflatex (texlive-bin)"; \
	 command -v makeindex >/dev/null || miss="$$miss\n    makeindex (texlive-binextra)"; \
	 for p in $(NEEDS); do \
	   kpsewhich $$p >/dev/null || miss="$$miss\n    $$p"; done; \
	 kpsewhich $(NEEDFONT) >/dev/null || \
	   miss="$$miss\n    Nimbus fonts (texlive-fontsrecommended)"; \
	 if [ -n "$$miss" ]; then \
	   echo "The manual cannot be built. Missing:"; \
	   printf '%b\n' "$$miss"; \
	   echo; \
	   echo "  sudo pacman -S texlive-basic texlive-latex \\"; \
	   echo "                 texlive-latexrecommended texlive-latexextra \\"; \
	   echo "                 texlive-pictures texlive-fontsrecommended"; \
	   exit 1; \
	 fi

# --- the build -------------------------------------------------------------
# Three passes: the first lays the pages out, the second settles the contents,
# the cross references and the thumb tabs (TikZ overlays, which need a pass of
# their own to know where they are), and the third picks up the index that
# makeindex builds in between.
$(OUT)/$(DOC).pdf: $(SOURCES) | $(OUT)
	@$(MAKE) --no-print-directory preflight
	@echo "  [1/4] pdflatex"
	@$(TEX) $(TEXFLAGS) $(DOC).tex > $(OUT)/pass1.log 2>&1 || \
	  { echo; sed -n '/^!/,/^$$/p;/:[0-9]*:/p' $(OUT)/$(DOC).log | head -60; exit 1; }
	@echo "  [2/4] pdflatex"
	@$(TEX) $(TEXFLAGS) $(DOC).tex > $(OUT)/pass2.log 2>&1 || \
	  { echo; sed -n '/^!/,/^$$/p;/:[0-9]*:/p' $(OUT)/$(DOC).log | head -60; exit 1; }
	@echo "  [3/4] makeindex"
	@if [ -f $(OUT)/$(DOC).idx ]; then \
	   makeindex -q -s style/ccindex.ist -o $(OUT)/$(DOC).ind $(OUT)/$(DOC).idx \
	     2> $(OUT)/makeindex.log || true; \
	 fi
	@echo "  [4/4] pdflatex"
	@$(TEX) $(TEXFLAGS) $(DOC).tex > $(OUT)/pass4.log 2>&1 || \
	  { echo; sed -n '/^!/,/^$$/p;/:[0-9]*:/p' $(OUT)/$(DOC).log | head -60; exit 1; }
	@echo
	@echo "  $(OUT)/$(DOC).pdf  --  `pdfinfo $(OUT)/$(DOC).pdf 2>/dev/null | awk '/^Pages/{print $$2}'` pages"
	@$(MAKE) --no-print-directory check

# One pass. Cross references and the index will be a build behind; use it to
# see whether a change compiles, not to check where anything landed.
quick: | $(OUT)
	@$(MAKE) --no-print-directory preflight
	@$(TEX) $(TEXFLAGS) $(DOC).tex > $(OUT)/quick.log 2>&1 || \
	  { sed -n '/^!/,/^$$/p;/:[0-9]*:/p' $(OUT)/$(DOC).log | head -60; exit 1; }
	@echo "built $(OUT)/$(DOC).pdf (one pass -- references may be stale)"

# --- the published copy ----------------------------------------------------
# pdf/ holds the built manual so it can be read, linked to and downloaded
# without a TeX installation anywhere near it. build/ is scratch and is
# gitignored; this copy is the artifact, and is meant to be committed.
#
# A file rule rather than a phony one, so `make pdf` twice does nothing the
# second time.
# The directory is made in the recipe rather than by an order-only rule: the
# target `pdf` and the directory `pdf/` are the same name, and make calls that
# a circular dependency and drops it.
pdf: $(PUB)/$(DOC).pdf

$(PUB)/$(DOC).pdf: $(OUT)/$(DOC).pdf
	@mkdir -p $(PUB)
	@cp $< $@
	@printf '  %s  --  %s, %s pages\n' "$@" \
	  "`du -h $@ | cut -f1`" \
	  "`pdfinfo $@ 2>/dev/null | awk '/^Pages/{print $$2}'`"

view: all
	@xdg-open $(OUT)/$(DOC).pdf >/dev/null 2>&1 &

# --- what the last build had to say ---------------------------------------
# Overfull boxes under 3pt are under a millimetre and not worth chasing, so
# only the ones you could actually see on the page are counted.
check:
	@test -f $(OUT)/$(DOC).log || { echo "nothing built yet"; exit 1; }
	@printf '  LaTeX warnings:   %s\n' \
	  "`grep -c 'LaTeX Warning' $(OUT)/$(DOC).log || true`"
	@printf '  Undefined refs:   %s\n' \
	  "`grep -c 'undefined' $(OUT)/$(DOC).log || true`"
	@printf '  Overfull > 3pt:   %s\n' \
	  "`grep -o 'Overfull .hbox ([0-9.]*pt' $(OUT)/$(DOC).log | \
	     grep -o '[0-9.]*' | awk '$$1>3' | wc -l`"

# \include writes one .aux per included file, and pdflatex will not create
# the subdirectories for them under -output-directory. So we do.
$(OUT):
	@mkdir -p $(OUT)/front $(OUT)/ch $(OUT)/app

# Leaves pdf/ alone: it is a published artifact, not a build product. Remove
# it by hand if you really mean to.
clean:
	rm -rf $(OUT)

env:
	@printf '%-22s %s\n' "pdflatex"  "`command -v $(TEX) || echo MISSING`"
	@printf '%-22s %s\n' "makeindex" "`command -v makeindex || echo MISSING`"
	@for p in $(NEEDS); do \
	   printf '%-22s %s\n' "$$p" "`kpsewhich $$p || echo MISSING`"; done
	@printf '%-22s %s\n' "Nimbus fonts" \
	  "`kpsewhich $(NEEDFONT) || echo MISSING`"
