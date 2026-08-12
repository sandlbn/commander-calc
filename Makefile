# x16sheet — dual-target build.
#
#   make            build the X16 program and its overlays
#   make test       build and run the host unit tests (fast, every change)
#   make run        launch in the emulator
#   make debug      launch in the emulator with the visual debugger
#   make echo       headless run, KERNAL output to stdout (for scripts/CI)
#   make clean
#
# Sources are split by directory and by filename suffix:
# Suffix decides the target, not directory: *_x16.c / *_x16.s are X16-only,
# *_host.c is host-only, everything else builds for both. src/ui is portable
# too — the grid's layout arithmetic is tested against a buffer backend.

# ---- toolchain ------------------------------------------------------
# cc65 was built from source into ~/.local. A non-default prefix must have
# CC65_HOME set or .macpack/.include lookups fail.
CC65_HOME ?= $(HOME)/.local/share/cc65
export CC65_HOME

CL65 ?= $(shell command -v cl65 2>/dev/null || echo $(HOME)/.local/bin/cl65)
EMU  ?= $(HOME)/Projects/x16-emulator/build/x16emu
ROM  ?= $(HOME)/Projects/x16-rom/build/x16/rom.bin

CC   ?= gcc

NAME    := CMDRCALC.PRG
BUILD   := build
OBJDIR  := $(BUILD)/obj
HOSTDIR := $(BUILD)/host

# ---- flags ----------------------------------------------------------
# --cpu 65c02 excludes Rockwell/65816 opcodes, per the project convention.
# How many overlays there are. MUST match OVL_COUNT in src/x16sheet.h.
#
# It was a hand-written list of numbers, in two places, and adding a
# fifteenth overlay did not add it to either -- so ld65 wrote the image and
# nothing turned it into the OVL15.BIN the loader asks for. It linked, it
# passed every test, and it would have failed on the machine the first time
# a chart was drawn.
OVL_N := 17

X16FLAGS := -t cx16 --cpu 65c02 -Osir -DX16S_XLSX_UI

# --codesize is cc65's size/speed dial: below 100 it stops expanding runtime
# helpers inline and calls them instead -- smaller code, an extra jsr on the
# operations it touches. It is set here for one directory, src/workbook, and
# the split is measured rather than reasoned:
#
#     everything at 100 (the default)          4 bytes of resident space
#     everything at 90                        35
#     src/workbook at 90, the rest at 100    729
#
# It is not monotonic and it is not intuitive. At 90 the UI, the evaluator,
# the importer and the exporter all get BIGGER -- src/ui alone at 90 is 510
# bytes worse than doing nothing. Only the workbook layer, which is dense
# 32-bit handle arithmetic against banked RAM, comes out ahead, and it comes
# out ahead by enough to pay for the next several features.
#
# So: measure before moving this. A directory added to CODESIZE_DIRS on the
# grounds that smaller ought to be smaller will very likely cost space.
#
# The speed given up is a jsr per helper in the code that reads and writes
# cells. It is not the recalculation -- src/formula and src/util/number are
# deliberately left at 100, because they are the slowest thing in the
# program and their bytes are worth less than their cycles.
CODESIZE_DIRS := src/workbook/% src/platform/banked_ram.c
SMALLFLAGS := $(X16FLAGS) --codesize 90

# Pick the flags for one source file.
ccflags = $(if $(filter $(CODESIZE_DIRS),$1),$(SMALLFLAGS),$(X16FLAGS))

LDFLAGS  := -t cx16 -C cfg/x16sheet.cfg -m $(BUILD)/x16sheet.map \
            -Ln $(BUILD)/labels.txt

HOSTFLAGS := -std=c99 -Wall -Wextra -Wno-unused-parameter -g -O1 -DX16S_HOST -DX16S_XLSX_UI \
             -DX16S_POOL_VERIFY
HOSTLIBS  := -lm

# ---- sources --------------------------------------------------------
# Suffix decides the target: *_x16.c/*_x16.s are X16-only, *_host.c is
# host-only, everything else in these directories builds for both.
ALL_C := $(wildcard src/util/*.c) $(wildcard src/workbook/*.c) \
         $(wildcard src/formula/*.c) $(wildcard src/import/*.c) \
         $(wildcard src/export/*.c) \
         $(wildcard src/platform/*.c) $(wildcard src/ui/*.c)
ALL_S := $(wildcard src/util/*.s) $(wildcard src/platform/*.s)

# file_io_csv_x16.c #includes file_io_x16.c to place a second, renamed copy
# in the CSV overlay. Both are compiled; neither includes the other's names.

# zipw.c and cells_iter.c are #included by the modules that need them and
# are all static; neither is ever compiled on its own, on either target.
PORTABLE := $(filter-out %_x16.c %_host.c src/export/zipw.c \
                         src/workbook/cells_iter.c \
                         src/workbook/sheet_new.c,$(ALL_C))

# blob.c is compiled into each overlay that needs it (blob_*_x16.c include
# it), never resident: see src/import/blob_ovl.h.
# blob.c has no segment of its own and would land resident, so only the
# per-overlay copies compile it. xml.c already names OVERLAY7, so it builds
# normally for OVL_XLSX and the renamed copies handle the other two.
# xlsx_import.c is the resident XLSX driver — the loop only, ~200 bytes; the
# work is in the xlsx_step_*.c files, which name their own overlays.
# xlsx_export.c is #included by src/export/xlsx_out{1,2}_x16.c, which
# compile it into their two overlays with XW_PART set; it is not a
# translation unit of its own on the X16. On the host there are no overlays
# and one copy holds both halves.
X16_PORTABLE := $(filter-out src/import/blob.c src/export/xlsx_export.c,\
                             $(PORTABLE))
X16_C    := $(X16_PORTABLE) $(filter %_x16.c,$(ALL_C)) src/main.c
X16_S    := $(filter %_x16.s,$(ALL_S))

HOST_C   := $(PORTABLE) $(filter %_host.c,$(ALL_C)) $(wildcard tests/*.c)

# Assembly objects get a distinct suffix: bankmem_x16.c and bankmem_x16.s
# would otherwise both want to be bankmem_x16.o.
X16_OBJ  := $(patsubst %.c,$(OBJDIR)/%.o,$(X16_C)) \
            $(patsubst %.s,$(OBJDIR)/%.s.o,$(X16_S))
HOST_OBJ := $(patsubst %.c,$(HOSTDIR)/%.o,$(HOST_C))

# Every object depends on every header. Coarse, but a full rebuild takes a
# few seconds and the alternative is silently linking stale objects — which
# is exactly how an edit to charmap.h once appeared to have no effect.
# file_io_x16.c is #included by the per-overlay copies (file_io_*_x16.c), so
# it is a prerequisite of them exactly like a header. Leaving it out means an
# edit to the file layer silently fails to reach any overlay's copy.
HEADERS := $(wildcard src/*.h) $(wildcard src/*/*.h) $(wildcard tests/*.h) \
           src/platform/file_io_x16.c src/import/blob.c src/import/xml.c \
           src/util/crc32.c src/export/xlsx_export.c src/export/zipw.c \
           src/workbook/cells_iter.c \
           src/workbook/cells_iter.c src/workbook/sheet_new.c
$(X16_OBJ) $(HOST_OBJ): $(HEADERS) Makefile

# ---- X16 ------------------------------------------------------------
.PHONY: all
all: x16

.PHONY: x16
x16: $(BUILD)/$(NAME)

$(BUILD)/$(NAME): $(X16_OBJ) cfg/x16sheet.cfg
	@mkdir -p $(BUILD)
	@# The link's warnings go through a sieve, and only this one.
	@#
	@# ld65 reports an "address size mismatch" for every zero-page
	@# variable another module reads -- 47 of them. All expected and all
	@# correct: a zero-page address IS a valid absolute address, and cc65
	@# emits .import rather than .importzp for an extern with no way in C
	@# to say otherwise (tested; see src/x16sheet.h). The count grows
	@# every time something moves to zero page, which is the technique
	@# holding this program together.
	@#
	@# They were burying the warnings that matter -- a signed-overflow
	@# warning caught a real bug in the chart clear loop and would not
	@# have been noticed in that pile. So they are COUNTED, not hidden,
	@# and every other line still comes through untouched.
	@echo "  ld65 -o $@"
	@$(CL65) $(LDFLAGS) -o $@ $(X16_OBJ) 2>$(BUILD)/link.log; st=$$?; \
	  grep -v 'Address size mismatch' $(BUILD)/link.log || true; \
	  n=$$(grep -c 'Address size mismatch' $(BUILD)/link.log || true); \
	  [ "$$n" -eq 0 ] || echo "  note: $$n zero-page cross-module reads, all expected (src/x16sheet.h)"; \
	  exit $$st
	@# ld65 writes overlay areas as raw images. KERNAL LOAD needs the
	@# 2-byte little-endian load address ($8000) in front of each. Keep
	@# this in step with __OVLSTART__ in cfg/x16sheet.cfg — an overlay
	@# loaded to the wrong address runs as whatever the bytes there mean.
	@for n in $$(seq 1 $(OVL_N)); do \
	    if [ -s $@.$$n ]; then \
	        printf '\000\200' | cat - $@.$$n > $(BUILD)/OVL$$n.BIN; \
	        rm -f $@.$$n; \
	    fi; \
	done
	@python3 tools/check_overlays.py $(OBJDIR) || \
	    { echo "  (an overlay calling another hangs the machine)"; exit 1; }
	@echo "--- size ---"
	@ls -l $(BUILD)/$(NAME) $(BUILD)/OVL*.BIN 2>/dev/null | awk '{printf "  %-22s %6d\n", $$9, $$5}'

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CL65) $(call ccflags,$<) -c -o $@ $<

$(OBJDIR)/%.s.o: %.s
	@mkdir -p $(dir $@)
	$(CL65) $(call ccflags,$<) -c -o $@ $<

# ---- host tests -----------------------------------------------------
.PHONY: host
host: $(HOSTDIR)/run_tests

$(HOSTDIR)/run_tests: $(HOST_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(HOSTFLAGS) -o $@ $(HOST_OBJ) $(HOSTLIBS)

$(HOSTDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(HOSTFLAGS) -c -o $@ $<

.PHONY: test
test: $(HOSTDIR)/run_tests
	@mkdir -p $(HOSTDIR)/sd
	@python3 tools/make_zip_fixtures.py $(HOSTDIR)/sd > /dev/null
	@python3 tools/make_xlsx_fixtures.py $(HOSTDIR)/sd > /dev/null
	@python3 tools/make_sheets_xlsx.py $(HOSTDIR)/sd > /dev/null
	@python3 tools/make_demo_xlsx.py $(HOSTDIR)/sd > /dev/null
	@$(HOSTDIR)/run_tests

# ---- on-target XLSX import test --------------------------------------
#
#   make xlsx                  run the pipeline in the emulator on DEMO.XLSX
#   make xlsx FILE=BOOK.XLSX   ...on another fixture
#
# A separate program with its own link, not a mode of x16sheet: the resident
# budget in the application belongs to the user interface, and a harness
# would not fit beside it. Its own link also means the importer is exercised
# with nothing else in the way.
#
# The host suite proves the parsing. This proves the parts only the machine
# has: that the overlay swaps happen in the right order, that the blobs live
# in banked RAM across bank boundaries, and that the ZIP reader really seeks
# on CMDR-DOS.
XLSXDIR := $(BUILD)/xlsxtest

# Everything except the user interface and the application's own main.
XT_C   := $(X16_PORTABLE) $(filter %_x16.c,$(filter-out src/ui/%,$(ALL_C))) \
          src/tools/xlsxtest.c
XT_C   := $(filter-out src/ui/%,$(XT_C))
XT_OBJ := $(patsubst %.c,$(XLSXDIR)/%.o,$(XT_C)) \
          $(patsubst %.s,$(XLSXDIR)/%.s.o,$(X16_S))

$(XT_OBJ): $(HEADERS) Makefile

$(XLSXDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CL65) $(X16FLAGS) -DX16S_POOL_VERIFY -c -o $@ $<

$(XLSXDIR)/%.s.o: %.s
	@mkdir -p $(dir $@)
	$(CL65) $(X16FLAGS) -DX16S_POOL_VERIFY -c -o $@ $<

$(XLSXDIR)/XLSXTEST.PRG: $(XT_OBJ) cfg/x16sheet.cfg
	@mkdir -p $(XLSXDIR)
	$(CL65) -t cx16 -C cfg/x16sheet.cfg -m $(XLSXDIR)/map.txt \
	    -o $@ $(XT_OBJ)
	@for n in $$(seq 1 $(OVL_N)); do \
	    if [ -s $@.$$n ]; then \
	        printf '\000\200' | cat - $@.$$n > $(XLSXDIR)/OVL$$n.BIN; \
	        rm -f $@.$$n; \
	    fi; \
	done

.PHONY: xlsx
xlsx: $(XLSXDIR)/XLSXTEST.PRG
	@mkdir -p $(HOSTDIR)/sd
	@python3 tools/make_demo_xlsx.py $(HOSTDIR)/sd > /dev/null
	@python3 tools/make_xlsx_fixtures.py $(HOSTDIR)/sd > /dev/null
	@python3 tools/make_sheets_xlsx.py $(HOSTDIR)/sd > /dev/null
	@cp $(HOSTDIR)/sd/*.XLSX $(XLSXDIR)/
	@python3 tools/check_overlays.py $(XLSXDIR) || exit 1
	@echo "--- running $(if $(FILE),$(FILE),DEMO.XLSX) in the emulator ---"
	@# 8 MHz deliberately: this is the test that says the importer works on
	@# the hardware, so it runs at the hardware's speed. -warp removes the
	@# frame-rate cap without touching the emulated clock.
	@timeout 180 stdbuf -o0 $(EMU) -rom $(ROM) -fsroot $(XLSXDIR) \
	    -prg $(XLSXDIR)/XLSXTEST.PRG -run -warp -mhz 8 -echo 2>/dev/null \
	    | sed -n '/xlsx import test/,/DONE/p'

# ---- inspect any .xlsx with the real pipeline ------------------------
# The fixtures only prove the importer agrees with the fixture generator.
# This runs the same code against a workbook nobody wrote for us:
#     make inspect FILE=/path/to/whatever.xlsx
INSPECT_OBJ := $(filter-out $(HOSTDIR)/tests/%,$(HOST_OBJ))

$(HOSTDIR)/inspect_xlsx: tools/inspect_xlsx.c $(INSPECT_OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(HOSTFLAGS) -o $@ $< $(INSPECT_OBJ) $(HOSTLIBS)

.PHONY: xlsxdemo
# Regenerate the sample .xlsx this program writes, for opening in a real
# spreadsheet. The host suite produces it as a side effect of the export
# round-trip test, which is also what proves it is worth looking at.
xlsxdemo: test
	@cp $(HOSTDIR)/sd/OUT.XLSX CMDRCALC-EXPORT.XLSX
	@python3 tools/check_xlsx.py CMDRCALC-EXPORT.XLSX
	@echo "wrote CMDRCALC-EXPORT.XLSX"

.PHONY: checkovl
# Nothing in one overlay may call into another; see tools/check_overlays.py.
# Run as part of `make x16`, because the failure mode is a hang with no
# diagnostic rather than anything the linker objects to.
checkovl:
	@python3 tools/check_overlays.py $(OBJDIR)

.PHONY: checkxlsx
checkxlsx:
	@python3 tools/check_xlsx.py $(if $(FILE),$(FILE),$(HOSTDIR)/sd/OUT.XLSX)

.PHONY: inspect
inspect: $(HOSTDIR)/inspect_xlsx
	@$(HOSTDIR)/inspect_xlsx $(FILE)

# ---- emulator -------------------------------------------------------
# -fsroot serves build/ over HostFS, so the program finds its overlays.
#
# MHZ overclocks the emulated CPU. A real Commander X16 is 8 MHz and nothing
# else, so this is a development convenience — inflating a worksheet is the
# slowest thing the program does and 16 MHz halves the wait while working on
# it. Anything being judged on how it will behave on the machine has to run
# at 8: `make run MHZ=8`, and the on-target harness below is pinned there.
MHZ := 16
# 2 MB of banked RAM, the most the emulator offers. The workbook heap is
# banked RAM and nothing else competes for it, so this is the difference
# between a few thousand cells and a few hundred thousand. The machine
# ships with 512 KB; anything being judged on how it will behave on the
# stock hardware wants `make run RAM=512`.
RAM := 2048
EMUFLAGS := -rom $(ROM) -fsroot $(BUILD) -prg $(BUILD)/$(NAME) -run \
            -mhz $(MHZ) -ram $(RAM)

# Workbooks to open on the machine, as opposed to fixtures to test against.
# Written into $(BUILD), which is what -fsroot serves, so `make examples run`
# puts them in front of F7 with no copying about.
.PHONY: examples
examples:
	@python3 tools/make_examples.py $(BUILD)

.PHONY: run
run: x16 examples
	$(EMU) $(EMUFLAGS)

# The same build on an emulated 65C816.
#
# Nothing here is compiled for the 816 -- the project convention is
# `--cpu 65c02` and it stays that way. The point is the opposite: the 816
# starts in emulation mode, where it runs 65C02 code, so this says whether
# the program still works on a machine fitted with one. Worth a run after
# touching hand-written assembly, which is where an opcode the two CPUs
# disagree about would come from.
.PHONY: runc816
runc816: x16 examples
	$(EMU) $(EMUFLAGS) -c816

.PHONY: debugc816
debugc816: x16
	$(EMU) $(EMUFLAGS) -c816 -debug

.PHONY: debug
debug: x16
	$(EMU) $(EMUFLAGS) -debug

.PHONY: echo
echo: x16
	timeout 30 $(EMU) $(EMUFLAGS) -warp -echo

# ---- the manual -----------------------------------------------------
# docs/manual has a Makefile of its own -- pdflatex three times plus
# makeindex, which is its own small world and does not belong in here.
# These just save you the cd.
MANUALDIR := docs/manual

.PHONY: manual
manual:
	@$(MAKE) --no-print-directory -C $(MANUALDIR)

# Build it and publish the result to docs/manual/pdf/, which is committed so
# the manual can be read without a TeX installation.
.PHONY: manualpdf
manualpdf:
	@$(MAKE) --no-print-directory -C $(MANUALDIR) pdf

.PHONY: manualview
manualview:
	@$(MAKE) --no-print-directory -C $(MANUALDIR) view

.PHONY: manualclean
manualclean:
	@$(MAKE) --no-print-directory -C $(MANUALDIR) clean

# ---- housekeeping ---------------------------------------------------
.PHONY: clean
clean:
	rm -rf $(BUILD)

.PHONY: env
env:
	@printf '%-10s %s\n' CL65 "$(CL65)" CC65_HOME "$(CC65_HOME)" \
	    EMU "$(EMU)" ROM "$(ROM)"
	@$(CL65) --version 2>&1 | head -1
	@test -f $(ROM) && echo "rom.bin present" || echo "rom.bin MISSING"
