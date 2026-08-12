/* test_xlsx_export.c — writing an .xlsx and reading it back.
 *
 * The round trip is the test that matters. An exporter can produce a file
 * that looks right in a hex dump and that no spreadsheet will open, and the
 * only cheap way to find out on this machine is to hand it to the importer
 * we already trust — which agrees with Excel, because it reads Excel's own
 * files in the on-target harness.
 *
 * What it cannot prove is that Excel itself is happy; tools/check_xlsx.py
 * does the structural half of that with python's zipfile.
 */
#include "test.h"
#include "../src/platform/bankmem.h"
#include "../src/platform/banked_ram.h"
#include "../src/platform/file_io.h"
#include "../src/workbook/workbook.h"
#include "../src/workbook/styles.h"
#include "../src/export/xlsx_export.h"
#include "../src/import/xlsx_import.h"

#define OUT "OUT.XLSX"

static void setup(void)
{
    CHECK(bankmem_host_init(255));
    bank_heap_init(1, 0);
    wb_init();
    file_host_set_root("build/host/sd");
}

static void disp(uint16_t r, uint16_t c, char *out)
{
    wb_display_text(r, c, out, WB_TEXT_MAX);
}

/* Values of every type this program stores, out and back. */
static void test_round_trip_values(void)
{
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(wb_set_text(0, 0, "Rent"), ERR_OK);
    CHECK_EQ(wb_set_text(0, 1, "1200.5"), ERR_OK);
    CHECK_EQ(wb_set_text(1, 0, "TRUE"), ERR_OK);
    CHECK_EQ(wb_set_text(2, 0, "a & b < c"), ERR_OK);   /* needs escaping */
    wb_set_col_width(0, 18);

    CHECK_EQ(xlsx_export(OUT), ERR_OK);

    /* Replaces the workbook, so what comes back cannot be what stayed. */
    CHECK_EQ(xlsx_import(OUT), ERR_OK);

    disp(0, 0, b); CHECK_STR(b, "Rent");
    disp(0, 1, b); CHECK_STR(b, "1200.5");
    disp(1, 0, b); CHECK_STR(b, "TRUE");
    disp(2, 0, b); CHECK_STR(b, "a & b < c");
    CHECK_EQ(wb_col_width(0), 18);
}

/* A formula keeps its source and its cached answer: a reader that does not
 * recalculate has to show something, and a reader that does needs the text. */
static void test_round_trip_formula(void)
{
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(wb_set_text(0, 0, "10"), ERR_OK);
    CHECK_EQ(wb_set_text(1, 0, "32"), ERR_OK);
    CHECK_EQ(wb_set_text(2, 0, "=A1+A2"), ERR_OK);
    wb_after_change();
    disp(2, 0, b); CHECK_STR(b, "42");

    CHECK_EQ(xlsx_export(OUT), ERR_OK);
    CHECK_EQ(xlsx_import(OUT), ERR_OK);

    /* The cached value, because translating formulas back into bytecode is
     * milestone 14. The point here is that the answer survived. */
    disp(2, 0, b); CHECK_STR(b, "42");
    CHECK(xlsx_result()->formulas > 0);
}

/* An empty workbook is still a valid package. */
static void test_round_trip_empty(void)
{
    setup();
    CHECK_EQ(xlsx_export(OUT), ERR_OK);
    CHECK_EQ(xlsx_import(OUT), ERR_OK);
    CHECK_EQ(xlsx_result()->cells, 0);
}

/* Everything DEMO.XLSX has, straight back out and in again: the widest
 * input this program has, exported without having been typed. */
static void test_reexport_demo(void)
{
    uint16_t cells, formulas;
    char a[WB_TEXT_MAX], b[WB_TEXT_MAX];
    static uint8_t before[40][8];
    cell_record_t rec;
    uint16_t rr, cc;

    setup();
    CHECK_EQ(xlsx_import("DEMO.XLSX"), ERR_OK);
    for (rr = 0; rr < 40; ++rr)
        for (cc = 0; cc < 8; ++cc)
            before[rr][cc] = wb_get(rr, cc, &rec) ? 1 : 0;
    cells = xlsx_result()->cells;
    formulas = xlsx_result()->formulas;
    disp(1, 1, a);                      /* a currency cell */

    CHECK_EQ(xlsx_export(OUT), ERR_OK);
    CHECK_EQ(xlsx_import(OUT), ERR_OK);

    /* Cell for cell, not merely the same count: a lost cell and a gained
     * one would cancel out in a total. */
    for (rr = 0; rr < 40; ++rr)
        for (cc = 0; cc < 8; ++cc)
            CHECK_EQ(before[rr][cc], wb_get(rr, cc, &rec) ? 1 : 0);

    /* Every worksheet, out and back: the same cell count that went in. */
    CHECK_EQ(xlsx_result()->cells, cells);
    CHECK_EQ(wb_sheet_n, 3);
    {
        char nm[X16S_MAX_SHEET_NAME + 1];
        wb_sheet_name(0, nm); CHECK_STR(nm, "Budget");
        wb_sheet_name(1, nm); CHECK_STR(nm, "Inventory");
        wb_sheet_name(2, nm); CHECK_STR(nm, "Summary");
    }
    /* And each sheet's own cells landed on it, rather than all on the
     * first: Inventory's A2 is a part name, Summary's A1 is its heading. */
    CHECK_EQ(wb_sheet_switch(1), ERR_OK);
    disp(1, 1, b); CHECK_STR(b, "Cable");
    CHECK_EQ(wb_sheet_switch(2), ERR_OK);
    disp(0, 0, b); CHECK_STR(b, "Summary");
    CHECK_EQ(wb_sheet_switch(0), ERR_OK);
    /* Formulas survive the whole trip now: Excel's text is compiled on the
     * way in, kept as a formula, and written back out as <f>. Not all of
     * them — COUNTBLANK is not a function this program has — and the ones
     * that do not compile keep the value Excel cached, so they come back as
     * numbers. Hence >= rather than ==. */
    CHECK(xlsx_result()->formulas > 0);
    CHECK(xlsx_result()->formulas <= formulas);
    CHECK(formulas > 0);
    disp(1, 1, b);
    CHECK_STR(b, a);                    /* format survived the trip */
    CHECK_EQ(wb_col_width(0), 18);
}

void test_xlsx_export(void)
{
    test_round_trip_values();
    test_round_trip_formula();
    test_round_trip_empty();
    test_reexport_demo();
}
