/* xlsxtest.c — run the XLSX import pipeline on the real machine.
 *
 * Built as its own program rather than into x16sheet, for two reasons. The
 * resident budget in the application is spent on the user interface, and a
 * harness would not fit beside it. And a separate link means this exercises
 * the pipeline without the grid, the editor or the dialogs in the way — if
 * something breaks here it is the importer.
 *
 * What it proves that the host suite cannot:
 *
 *   - the overlay swaps really happen, in the right order, and each phase
 *     finds its code where it expects it
 *   - the blobs really live in banked RAM, across bank boundaries
 *   - the ZIP reader really seeks on CMDR-DOS, on a file the emulator is
 *     serving over HostFS
 *
 * Output goes straight to CHROUT, and NOT through printf.
 *
 * printf cannot be used in this program, which is worth explaining because
 * it looks like it should work and fails silently. src/charmap.h turns off
 * cc65's ASCII-to-PETSCII translation of string literals, so "%lu" reaches
 * printf as ASCII. But the C library was compiled with the stock cx16
 * charmap, where a lowercase letter in a literal becomes a PETSCII code in
 * the $41..$5A range — so the 'l' and 'u' printf compares against are not
 * the 'l' and 'u' in our format string. Punctuation matches, letters do
 * not. The result is that '%' is recognised, the conversion letter is not,
 * and the value is dropped: "banks %u" prints as "banks ".
 *
 * The application never hits this because it draws through screen.h rather
 * than stdio. Anything that does want formatted output has to build it, as
 * below.
 */

#include <string.h>
#include <cbm.h>
#include <time.h>

#include "../x16sheet.h"
#include "../platform/bankmem.h"
#include "../platform/banked_ram.h"
#include "../platform/overlay.h"
#include "../platform/file_io.h"
#include "../workbook/workbook.h"
#include "../workbook/cells.h"
#include "../workbook/strings.h"
#include "../import/blob.h"
#include "../import/inflate.h"
#include "../import/xlsx.h"
#include "../import/xlsx_stage.h"
#include "../import/xlsx_sheet.h"
#include "../import/xlsx_import.h"
#include "../export/xlsx_export.h"
#include "../workbook/native_file.h"
#include "../platform/banked_ram.h"

/* Too large for the stack, and they must outlive the overlay swaps. */
static blob_t      staged, part;
static xlsx_target_t target;
static handle_t    ids, styles;
static xlsx_styles_info_t sinfo;
static uint16_t nstrings;

static const char ARCHIVE[] = "DEMO.XLSX";

/* CHROUT reads bytes as PETSCII, where ASCII 'S' is a lowercase s -- so
 * "Smallest" reaches the screen as "sMALLEST". ISO mode makes CHROUT take
 * ASCII directly, which is what every string here already is.
 *
 * Only the harness needs this: the grid writes tile indices straight to
 * VERA and never goes through CHROUT. */
static void iso_mode(void) { cbm_k_bsout(0x0F); }

static void put(const char *s)
{
    while (*s)
        cbm_k_bsout((unsigned char)*s++);
}

static void put_ln(void) { cbm_k_bsout(13); }

static void put_u32(uint32_t v)
{
    char d[11];
    uint8_t n = 0;

    if (v == 0) {
        cbm_k_bsout('0');
        return;
    }
    while (v) {
        d[n++] = (char)('0' + (uint8_t)(v % 10));
        v /= 10;
    }
    while (n)
        cbm_k_bsout((unsigned char)d[--n]);
}

/* One part, through both phases. Each ovl_require is a load from the card;
 * getting them in the wrong order calls into whatever the previous overlay
 * left at $8000, which is the failure this whole harness exists to catch. */
static err_t fetch(const char *path)
{
    stage_info_t info;
    err_t e;

    /* No blob_free here: blob_alloc releases whatever the target held, and
     * inflate_blob consumes its source. The blob layer lives inside the
     * overlays, so resident code cannot call into it. */
    if (ovl_require(OVL_ZIP) != ERR_OK)
        return ERR_IO;
    e = xlsx_stage(ARCHIVE, path, &staged, &info);
    if (e != ERR_OK)
        return e;

    if (!info.compressed) {
        part = staged;
        memset(&staged, 0, sizeof staged);
        return ERR_OK;
    }

    if (ovl_require(OVL_INFLATE) != ERR_OK)
        return ERR_IO;
    return inflate_blob(&staged, &part, info.raw_size);
}

/* Print the imported cells as a table.
 *
 * Counts only prove the parser reached the end. A currency column read as
 * raw serials, a date off by the phantom leap day, or a shared string
 * resolved to the wrong entry all give perfect counts, so seeing the sheet
 * is the test.
 *
 * Rendered through wb_display_text, so this is exactly what the grid would
 * draw, number formats and all.
 */
static void pad_to(uint8_t from, uint8_t to)
{
    while (from++ < to)
        cbm_k_bsout(' ');
}

static void show_grid(void)
{
    char b[WB_TEXT_MAX];
    const cellstore_t *cs = wb_cells();
    uint16_t row, col;
    uint8_t n, x, k;
    uint16_t last_row = cs->max_row < 17 ? cs->max_row : 17;
    uint16_t last_col = cs->max_col < 6 ? cs->max_col : 6;

#define COL_W 11

    put_ln();
    put("first cells, as the grid would draw them:");
    put_ln();

    for (row = 0; row <= last_row; ++row) {
        x = 0;
        put_u32(row + 1);                   /* 1-based, as on screen */
        x = (uint8_t)(row < 9 ? 1 : 2);
        pad_to(x, 4);
        x = 4;
        for (col = 0; col <= last_col; ++col) {
            n = wb_display_text(row, col, b, sizeof b);
            /* Clipped to the column, the way the grid clips it — a value
             * that ran over would misalign every column after it and make
             * the table harder to read than the numbers it replaced. */
            if (n > COL_W - 1) {
                b[COL_W - 1] = '\0';
                n = COL_W - 1;
            }
            for (k = 0; k < n; ++k)
                cbm_k_bsout((unsigned char)b[k]);
            x = (uint8_t)(x + n);
            pad_to(x, (uint8_t)(4 + (col + 1) * COL_W));
            x = (uint8_t)(4 + (col + 1) * COL_W);
        }
        put_ln();
    }
}

static void report(const char *what, err_t e)
{
    put(what);
    put(": ");
    put(e == ERR_OK ? "ok" : "FAILED");
    put_ln();
}

/* --- how much C stack the deepest path actually uses ------------------
 *
 * Every byte of __STACKSIZE__ is a byte of resident memory nothing else can
 * have, so the reservation is worth measuring rather than arguing about.
 * Paint the region, run the deepest chain the program has -- the .xlsx
 * import, driver into step into parser into tokenizer into blob -- and what
 * is left unpainted is the margin.
 *
 * THE TOP `RESERVE` BYTES ARE LEFT ALONE because main()'s own frame is in
 * them while the painting runs; cc65 puts locals on the C stack unless
 * built with -Cl, which this is not. Anything below RESERVE is therefore
 * invisible, so what this answers is whether the deepest path went past
 * that mark.
 */
#define STK_TOP  0x8000
#define RESERVE  192
extern uint8_t *const stack_floor;      /* stackfloor_x16.s */

static void stack_paint(void)
{
    uint8_t *p = stack_floor;

    while (p < (uint8_t *)(STK_TOP - RESERVE))
        *p++ = 0xA5;
}

static uint16_t stack_used(void)
{
    uint8_t *p = stack_floor;

    while (p < (uint8_t *)STK_TOP && *p == 0xA5)
        ++p;
    return (uint16_t)(STK_TOP - (uint16_t)p);
}

int main(void)
{
    err_t e;
    uint8_t i;
    char buf[64];

    /* Exactly as main() does, and for the same reason: the golden statics
     * are not cleared by the startup code. A harness that skipped this
     * would be running a machine the application never runs on. */
    golden_init();
    stack_paint();

    put_ln();
    iso_mode();
    put("xlsx import test");
    put_ln();

    if (bank_heap_init(1, 0) != ERR_OK) {
        put("no banked ram"); put_ln();
        return 1;
    }
    wb_init();
    put("banks "); put_u32(bankmem_bank_count());
    put(", free "); put_u32(bank_bytes_free()); put_ln();

    ids = bank_alloc(2048);
    styles = bank_calloc(XLSX_STYLE_BYTES);

    /* --- every sheet, exactly as the driver sequences it -------------- */
    for (i = 0; ; ++i) {
        xlsx_sheet_result_t r;

        e = fetch("xl/workbook.xml");
        if (e != ERR_OK) { report("stage workbook", e); return 1; }
        if (ovl_require(OVL_XLSX) != ERR_OK) return 1;
        e = xlsx_find_sheet(&part, i, &target);
        if (e == ERR_NOTFOUND)
            break;                      /* past the last sheet */
        if (e != ERR_OK) { report("parse workbook", e); return 1; }

        if (i == 0) {
            put("sheets "); put_u32(target.sheet_count); put_ln();

            e = fetch("xl/sharedStrings.xml");
            if (e == ERR_OK) {
                if (ovl_require(OVL_XLSX) != ERR_OK) return 1;
                e = xlsx_parse_strings(&part, ids, 1024, &nstrings, 0);
                report("shared strings", e);
                put("  strings "); put_u32(nstrings); put_ln();
            }
            e = fetch("xl/styles.xml");
            if (e == ERR_OK) {
                if (ovl_require(OVL_STYLE) != ERR_OK) return 1;
                e = xlsx_parse_styles(&part, styles, &sinfo);
                report("styles", e);
                put("  formats "); put_u32(sinfo.count); put_ln();
            }
        }

        e = fetch("xl/_rels/workbook.xml.rels");
        if (e != ERR_OK) { report("stage rels", e); return 1; }
        if (ovl_require(OVL_XLSX) != ERR_OK) return 1;
        e = xlsx_find_path(&part, &target);
        if (e != ERR_OK) { report("find path", e); return 1; }

        e = fetch(target.u.path);
        if (e != ERR_OK) { report("stage sheet", e); return 1; }
        if (ovl_require(OVL_SHEET) != ERR_OK) return 1;
        e = xlsx_parse_sheet(&part, ids, nstrings,
                             styles, sinfo.count, &r);

        put("sheet "); put_u32(i); put(" "); put(target.name);
        put(": "); put(e == ERR_OK ? "ok" : "FAILED");
        put(" cells "); put_u32(r.cells);
        put(" formulas "); put_u32(r.formulas);
        put(" dates "); put_u32(r.dates);
        put_ln();
        if (e != ERR_OK) return 1;

        /* Show the first sheet before the others are layered on top of it.
         * This harness deliberately does not reset between sheets — that
         * proves each one parses — but it means the cell space is shared,
         * so the only sheet worth looking at is the first. The real driver
         * resets and imports one. */
        if (i == 0)
            show_grid();
    }


    /* --- and back out again -------------------------------------------
     *
     * The one thing the host suite cannot check: the writer spans two
     * overlays, hands its archive state through banked RAM, and keeps a
     * KERNAL file open across the swap. On the host every overlay is linked
     * into one binary and ovl_require() is a no-op, so none of that is
     * exercised there. Here it is real.
     */
    {
        xlsx_report_t rep;
        err_t xe;

        put("export: ");
        xe = xlsx_export("OUT.XLSX");
        put(xe == ERR_OK ? "ok" : "FAILED");
        put_ln();
        if (xe != ERR_OK) return 1;

        put("re-import: ");
        xe = xlsx_import("OUT.XLSX");
        rep = *xlsx_result();
        put(xe == ERR_OK ? "ok" : "FAILED");
        put(" cells "); put_u32(rep.cells);
        put(" strings "); put_u32(rep.strings);
        put_ln();
        if (xe != ERR_OK) return 1;
        show_grid();
    }

    /* --- and through the native format ---------------------------------
     *
     * Imports three sheets, saves, throws the workbook away and reloads --
     * on the machine, where the writer and the reader are in different
     * overlays and the sheet table lives in banked RAM. */
    {
        uint8_t n;
        err_t   se;

        /* Through the real driver, so the workbook genuinely holds three
         * worksheets rather than the one this harness builds by hand. */
        se = xlsx_import("DEMO.XLSX");
        put("import all: "); put(se == ERR_OK ? "ok" : "FAILED");
        put(" sheets "); put_u32(wb_sheet_n); put_ln();
        if (se != ERR_OK) return 1;


        /* Is the pool sound before anything is written from it? This is
         * where the free-list fault would have been caught, rather than in
         * a saved file sixty kilobytes later. */
        put("pool: "); put_u32(strpool_verify()); put(" bad"); put_ln();

        /* Out again as a .xlsx, now that the workbook really holds three
         * worksheets: the writer spans two overlays and walks the sheets
         * between them, which is the part the host cannot exercise. */
        se = xlsx_export("OUT3.XLSX");
        put("export3: "); put(se == ERR_OK ? "ok" : "FAILED"); put_ln();
        if (se != ERR_OK) return 1;

        /* The overlay each of these lives in has to be loaded first. The
         * application does it in run_command(); this harness is resident
         * code calling straight in, and forgetting it is a hang, not an
         * error. tools/check_overlays.py catches exactly this -- run it
         * against build/xlsxtest, not just build/obj. */
        if (ovl_require(OVL_X16S_SAVE) != ERR_OK) return 1;
        se = x16s_save("RT.X16S");
        put("x16s save: "); put(se == ERR_OK ? "ok" : "FAILED"); put_ln();
        if (se != ERR_OK) return 1;

        wb_reset();
        if (ovl_require(OVL_FILEIO) != ERR_OK) return 1;
        se = x16s_open("RT.X16S");
        n = wb_sheet_n;
        put("x16s load: "); put(se == ERR_OK ? "ok" : "FAILED");
        put(" sheets "); put_u32(n); put_ln();
        if (se != ERR_OK) return 1;

        /* And the sheets are the ones that went in, with their names. */
        {
            uint8_t i;
            for (i = 0; i < n; ++i) {
                /* wb_tab_name, not wb_sheet_name: the full one is in
                 * OVL_FILEDLG and this is resident code that has not
                 * loaded it. check_overlays.py caught that before it ran. */
                char nm[WB_TAB_NAME];
                wb_tab_name(i, nm);
                put("  sheet "); put_u32(i); put(" "); put(nm); put_ln();
            }
        }

        /* And the FORMULAS came back, which is the point of the chunk: a
         * saved formula written as a banked handle means nothing after a
         * reload. Budget has a column of them and B21 is the SUM at the
         * foot of it.
         *
         * Printed rather than asserted, so a wrong answer is visible beside
         * the right one in the listing above. */
        {
            uint8_t  s2;
            uint16_t nf = 0;
            char v[WB_TEXT_MAX];
            uint16_t fr = 0;
            uint8_t  fc = 0, fs = 0, got = 0;

            /* Through wb_get, because the row walkers are overlay code and
             * this is resident. Bounded by the used range, so it is a few
             * thousand lookups on DEMO and not sixteen million. */
            for (s2 = 0; s2 < n; ++s2) {
                const cellstore_t *cs;
                uint16_t row;
                wb_sheet_switch(s2);
                cs = wb_cells();
                if (!cs->cell_count)
                    continue;
                for (row = 0; row <= cs->max_row; ++row) {
                    uint16_t col;
                    for (col = 0; col <= cs->max_col; ++col) {
                        cell_record_t rc;
                        if (wb_get(row, col, &rc)
                            && rc.type == CELL_FORMULA) {
                            ++nf;
                            if (!got) {
                                got = 1; fs = s2; fr = row;
                                fc = (uint8_t)col;
                            }
                        }
                    }
                }
            }
            put("  formulas after reload: "); put_u32(nf); put_ln();
            if (got) {
                wb_sheet_switch(fs);
                wb_display_text(fr, fc, v, sizeof v);
                put("  first one reads: "); put(v); put_ln();
            }
            wb_sheet_switch(0);
        }
    }

    /* A range covering the cell the formula is in.
     *
     * HERE RATHER THAN IN THE HOST SUITE BECAUSE THE HOST CANNOT FAIL IT:
     * the fault it guards is cc65 not unwinding the C stack for a goto out
     * of a block, and gcc unwinds correctly. Anything taking the early exit
     * from an aggregate's range walk exercises it; a self-referencing range
     * is the easy way to ask. */
    {
        static const char *const cyc[] = { "=SUM(A1:A6)", "=COUNT(A1:A6)",
                                           "=MAX(A1:A6)", 0 };
        char v[WB_TEXT_MAX];
        uint8_t i, k;

        for (i = 0; cyc[i]; ++i) {
            wb_reset();
            for (k = 0; k < 5; ++k) {
                char n[4]; n[0] = (char)('1' + k); n[1] = 0;
                wb_set_text(k, 0, n);
            }
            wb_set_text(5, 0, cyc[i]);
            wb_display_text(5, 0, v, sizeof v);
            put("self-range "); put(cyc[i]); put(": "); put(v);
            put(v[0] == '#' ? "  ok" : "  FAILED");
            put_ln();
        }

        /* The same shape across sheets, which the single-sheet scheduler
         * could not have detected before formula_recalc swept them all. */
        wb_reset();
        /* wb_sheet_add is in OVL_FILEDLG, and this is resident code --
         * check_overlays.py said so before this ever ran. */
        if (ovl_require(OVL_FILEDLG) != ERR_OK) return 1;
        wb_sheet_add("Two");
        wb_sheet_switch(0);
        wb_set_text(0, 0, "=Two!A1+1");
        wb_sheet_switch(1);
        wb_set_text(0, 0, "=Sheet1!A1+1");
        wb_sheet_switch(0);
        wb_display_text(0, 0, v, sizeof v);
        put("cross-sheet cycle: "); put(v);
        put(v[0] == '#' ? "  ok" : "  FAILED"); put_ln();

        /* Nesting, which is what the C stack fix was really about. */
        wb_reset();
        for (k = 0; k < 5; ++k) {
            char n[4]; n[0] = (char)('1' + k); n[1] = 0;
            wb_set_text(k, 0, n);
        }
        wb_set_text(6, 0, "=ROUND(AVERAGE(A1:A5)*2+MAX(A1:A5),2)");
        wb_display_text(6, 0, v, sizeof v);
        put("nested calls: "); put(v); put_ln();
        wb_set_text(7, 0, "=IF(SUM(A1:A5)>3,MAX(A1:A5),MIN(A1:A5))");
        wb_display_text(7, 0, v, sizeof v);
        put("nested IF: "); put(v); put_ln();
        put("peak stack after those: "); put_u32(stack_used()); put_ln();
    }

    /* The largest workbook that imports, and the analysis sheet on top of
     * it. 500 trading days of real NASDAQ Composite data with a Summary
     * sheet that reaches across for its highs and lows. */
    {
        err_t ne;
        uint32_t t0, t1;
        char v[WB_TEXT_MAX];

        ne = xlsx_import("NASDAQ.XLSX");
        put("NASDAQ: "); put(ne == ERR_OK ? "ok" : "FAILED");
        put("  cells "); put_u32(xlsx_result()->cells);
        put("  formulas "); put_u32(xlsx_result()->formulas_live);
        put("/"); put_u32(xlsx_result()->formulas);
        put("  free "); put_u32(bank_bytes_free()); put_ln();
        if (ne == ERR_OK) {
            uint8_t r;
            wb_sheet_switch(1);                  /* Summary */
            for (r = 0; r < 4; ++r) {
                wb_display_text(r, 0, v, sizeof v); put("  "); put(v);
                wb_display_text(r, 2, v, sizeof v); put("  high "); put(v);
                wb_display_text(r, 3, v, sizeof v); put("  low "); put(v);
                wb_display_text(r, 5, v, sizeof v); put("  days "); put(v);
                put_ln();
            }

            t0 = (uint32_t)clock();
            wb_set_text(30, 0, "1");
            t1 = (uint32_t)clock();
            /* One edit, which recalculates the workbook. 434 jiffies --
             * 7.2 seconds -- before the sheet note in each_formula. */
            put("  edit + recalc: "); put_u32(t1 - t0);
            put(" jiffies"); put_ln();
        }
    }

    /* --- RAIN.X16S, cell by cell ---------------------------------------
     *
     * The cross-sheet example: Summary reaches into Readings for every one
     * of its answers. The host loads this file correctly, so anything wrong
     * with it is wrong only on the machine.
     *
     * Reported cell by cell rather than as a verdict, because "some cells
     * say #ERR" has to become "these cells, holding this". */
    {
        err_t re;
        char v[WB_TEXT_MAX];

        wb_reset();
        if (ovl_require(OVL_FILEIO) != ERR_OK) return 1;
        re = x16s_open("RAIN.X16S");
        put("RAIN: "); put(re == ERR_OK ? "ok" : "FAILED");
        put(" sheets "); put_u32(wb_sheet_n); put_ln();
        if (re == ERR_OK) {
            uint8_t sh;

            /* x16s_open compiles the formulas; recalculating is what the
             * application does next, and it is the step that would turn a
             * good cached value into a bad computed one. */
            wb_after_change();

            for (sh = 0; sh < wb_sheet_n; ++sh) {
                const cellstore_t *cs;
                uint16_t r, c;

                wb_sheet_switch(sh);
                cs = wb_cells();
                put(" sheet "); put_u32(sh);
                put(" rows "); put_u32((uint32_t)cs->max_row + 1);
                put(" cols "); put_u32((uint32_t)cs->max_col + 1); put_ln();

                for (r = 0; r <= cs->max_row; ++r)
                    for (c = 0; c <= cs->max_col; ++c) {
                        cell_record_t rec;

                        if (!cells_get(cs, r, c, &rec))
                            continue;
                        wb_display_text(r, c, v, sizeof v);
                        if (v[0] != '#')
                            continue;
                        put("  r"); put_u32((uint32_t)r + 1);
                        put(" c"); put_u32(c);
                        put(" = "); put(v);
                        wb_edit_text(r, c, v, sizeof v);
                        put("   src "); put(v); put_ln();
                    }
            }
        }
    }

    put("stack: "); put_u32(stack_used());

    put(" of "); put_u32(STK_TOP - (uint16_t)stack_floor); put_ln();

    put("free at end "); put_u32(bank_bytes_free()); put_ln();
    put("DONE"); put_ln();
    return 0;
}
