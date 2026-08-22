/* test_xlsx_import.c — the driver, end to end.
 *
 * Everything else in the XLSX tests exercises one parser against one part.
 * This runs the whole sequence the way the program does: ten overlay swaps
 * across five overlays, with the state that survives them living in one
 * resident struct. The parts individually being right does not make the
 * sequence right — the driver is where a part can be fetched in the wrong
 * order, a handle can be lost across a swap, or a step can name the wrong
 * overlay to go to next, and none of those are visible from a parser test.
 *
 * DEMO.XLSX is the acceptance workbook: three sheets, of which the first is
 * imported and the other two reported as skipped.
 */
#include "test.h"
#include "../src/platform/bankmem.h"
#include "../src/platform/banked_ram.h"
#include "../src/platform/file_io.h"
#include "../src/workbook/workbook.h"
#include "../src/import/xlsx_import.h"

static void setup(void)
{
    CHECK(bankmem_host_init(255));
    bank_heap_init(1, 0);
    wb_init();
    file_host_set_root("build/host/sd");
}

/* An imported formula must keep the '=' its source needs.
 *
 * .xlsx stores a formula's text WITHOUT one -- the <f> element holds
 * "SUM(B1:B9)", not "=SUM(B1:B9)". The formula bar shows this text, and so
 * does the clipboard, so an imported formula that is copied and pasted
 * lands as a LABEL rather than as a formula. */
static void test_imported_formula_keeps_its_equals(void)
{
    cell_record_t rec;
    char b[WB_TEXT_MAX];
    uint16_t r, c;
    uint8_t found = 0;

    setup();
    CHECK_EQ(xlsx_import("DEMO.XLSX"), ERR_OK);

    for (r = 0; r < 40 && !found; ++r)
        for (c = 0; c < 12; ++c)
            if (wb_get(r, c, &rec) && rec.type == CELL_FORMULA) {
                wb_edit_text(r, c, b, sizeof b);
                CHECK_EQ(b[0], '=');
                found = 1;
                break;
            }
    CHECK(found);
}

static void test_demo_workbook(void)
{
    const xlsx_report_t *r;

    setup();
    CHECK_EQ(xlsx_import("DEMO.XLSX"), ERR_OK);

    r = xlsx_result();
    /* All three sheets of the acceptance workbook: 106 + 1208 + 21 cells
     * and 32 + 401 + 10 formulas, each measured on hardware. The report
     * covers the workbook, not the sheet the importer stopped on. */
    CHECK_EQ(r->cells, 1335);
    CHECK_EQ(r->formulas, 443);
    CHECK_EQ(r->dates, 12);
    CHECK_EQ(r->sheets_found, 3);
    CHECK_EQ(r->sheets_skipped, 0);
    CHECK(r->strings > 0);
    CHECK(r->formats > 0);
    CHECK_EQ(r->truncated, 0);
    {   /* the first sheet keeps its name from the file */
        char name[X16S_MAX_SHEET_NAME + 1];
        wb_sheet_name(0, name);
        CHECK_STR(name, "Budget");
    }
}

/* The cells have to be in the workbook, not merely counted: an early version
 * reported everything and stored nothing after the first row block. */
static void test_cells_landed(void)
{
    cell_record_t c;
    uint16_t row, found = 0;

    setup();
    CHECK_EQ(xlsx_import("DEMO.XLSX"), ERR_OK);
    for (row = 0; row < 40; ++row)
        if (wb_get(row, 0, &c) && c.type != CELL_EMPTY)
            ++found;
    CHECK(found > 0);
}

/* A file that is not a ZIP at all must say so rather than fail somewhere
 * deeper, and must leave the workbook usable. */
static void test_not_an_xlsx(void)
{
    setup();
    CHECK(xlsx_import("TEST.CSV") != ERR_OK);
    CHECK_EQ(xlsx_result()->cells, 0);
}

/* Twice in a row: the driver resets its own state, and the second import
 * must not inherit the first one's banks, handles or counters. Running out
 * of banked RAM on the second run is the classic way a leak shows up. */
static void test_import_twice(void)
{
    uint16_t first;

    setup();
    CHECK_EQ(xlsx_import("DEMO.XLSX"), ERR_OK);
    first = xlsx_result()->cells;
    CHECK_EQ(xlsx_import("DEMO.XLSX"), ERR_OK);
    CHECK_EQ(xlsx_result()->cells, first);
}

/* <cols> widths, and the reset hazard they exposed.
 *
 * The width table lives in banked RAM and xlsx_import() calls wb_reset(),
 * which re-initialises the whole heap. A table owned by anything that is
 * not re-allocated at that moment goes on pointing at an address the
 * importer has since handed to something else — the first version of this
 * wrote column widths over the shared-string map, and the symptom was two
 * header cells coming back empty while the rest of the sheet was fine.
 *
 * So this checks both halves: the widths arrived, AND the strings that
 * shared their address did not go missing. */
static void test_col_widths(void)
{
    setup();
    CHECK_EQ(xlsx_import("DEMO.XLSX"), ERR_OK);

    /* <col min="1" max="1" width="18.5"/>, and the fraction is dropped:
     * Excel measures in a proportional font and we draw in a fixed one. */
    CHECK_EQ(wb_col_width(0), 18);
    CHECK_EQ(wb_col_width(1), 12);      /* min=2 max=5 width=12.0 */
    CHECK_EQ(wb_col_width(4), 12);
    CHECK_EQ(wb_col_width(5), 14);      /* min=6 max=6 width=14.0 */
    CHECK_EQ(wb_col_width(9), X16S_DEF_COL_W);   /* not mentioned */

    /* And they are the first sheet's widths, not a later sheet's leaking
     * back: Inventory sets its own and the importer visits it afterwards. */
    CHECK_EQ(wb_sheet_i, 0);
    CHECK_EQ(xlsx_result()->cells, 1335);
}

/* A width is clamped, not trusted: Excel's own default for a hidden column
 * is 0, and a sheet can ask for 255. */
static void test_width_clamped(void)
{
    setup();
    wb_set_col_width(3, 1);
    CHECK_EQ(wb_col_width(3), X16S_MIN_COL_W);
    wb_set_col_width(3, 200);
    CHECK_EQ(wb_col_width(3), X16S_MAX_COL_W);
    wb_set_col_width(300, 20);          /* no such column; must not write */
    CHECK_EQ(wb_col_width(3), X16S_MAX_COL_W);
}

/* Milestone 14: an imported formula is a formula, not a frozen number.
 *
 * The importer used to keep Excel's cached answer and throw the text away,
 * so a SUM showed the right total and then never changed again. This checks
 * the two halves of the fix: that the text compiled, and that the result
 * moves when an input does. */
static void test_imported_formula_recalculates(void)
{
    char b[WB_TEXT_MAX];
    const xlsx_report_t *r;

    setup();
    CHECK_EQ(xlsx_import("DEMO.XLSX"), ERR_OK);
    r = xlsx_result();

    CHECK(r->formulas_live > 0);
    CHECK(r->formulas_live <= r->formulas);

    /* B14 is SUM(B2:B13) over a currency column. The format has to survive
     * compilation too — a fresh cell has style 0, and the first version of
     * this returned 3531.18 where Excel had $3531.18. */
    wb_display_text(13, 1, b, sizeof b);
    CHECK_STR(b, "$3531.18");

    /* Change one of its inputs. A cached number would not budge. */
    CHECK_EQ(wb_set_text(1, 1, "1300.5"), ERR_OK);
    wb_after_change();
    wb_display_text(13, 1, b, sizeof b);
    CHECK_STR(b, "$3631.68");
}

/* A function this program does not have keeps Excel's answer rather than
 * becoming an error or an empty cell. C18 is COUNTBLANK(A1:A2). */
static void test_unsupported_formula_keeps_value(void)
{
    char b[WB_TEXT_MAX];
    cell_record_t rec;

    setup();
    CHECK_EQ(xlsx_import("DEMO.XLSX"), ERR_OK);
    CHECK(wb_get(17, 2, &rec));
    CHECK(rec.type != CELL_FORMULA);
    wb_display_text(17, 2, b, sizeof b);
    CHECK_STR(b, "0");
}

/* Every sheet, not just the first.
 *
 * SHEETS.XLSX exists for this: five worksheets with different row counts,
 * column widths and names, so a sheet that silently did not arrive, or
 * arrived on top of another, is visible in the numbers. */
static void test_all_sheets(void)
{
    char b[WB_TEXT_MAX];
    char name[X16S_MAX_SHEET_NAME + 1];

    setup();
    CHECK_EQ(xlsx_import("SHEETS.XLSX"), ERR_OK);
    CHECK_EQ(wb_sheet_n, 5);
    CHECK_EQ(xlsx_result()->sheets_found, 5);
    CHECK_EQ(xlsx_result()->sheets_skipped, 0);

    /* The importer leaves the user on the first sheet. */
    CHECK_EQ(wb_sheet_i, 0);
    wb_sheet_name(0, name); CHECK_STR(name, "First");
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "first sheet row 1");

    /* Each sheet kept its own name, contents and column width. */
    CHECK_EQ(wb_sheet_switch(1), ERR_OK);
    wb_sheet_name(1, name); CHECK_STR(name, "Second");
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "second sheet row 1");
    CHECK_EQ(wb_col_width(0), 10);

    CHECK_EQ(wb_sheet_switch(2), ERR_OK);
    wb_sheet_name(2, name); CHECK_STR(name, "Third");
    CHECK_EQ(wb_col_width(0), 30);

    CHECK_EQ(wb_sheet_switch(4), ERR_OK);
    wb_sheet_name(4, name); CHECK_STR(name, "Fifth");
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "fifth sheet has one row 1");
}

/* A formula on a later sheet must compile into that sheet. The batch is
 * compiled after the whole import, when the active sheet is the last one,
 * so an entry that forgot which sheet it came from would land somewhere
 * else entirely -- silently, and with the right-looking number on it. */
static void test_formulas_land_on_their_own_sheet(void)
{
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(xlsx_import("SHEETS.XLSX"), ERR_OK);

    /* Second has 12 rows, so C12 is SUM(B1:B12) = 100+200+...+1200. */
    CHECK_EQ(wb_sheet_switch(1), ERR_OK);
    wb_display_text(11, 2, b, sizeof b);
    CHECK_STR(b, "7800");

    /* Third has 3 rows: 100+200+300. */
    CHECK_EQ(wb_sheet_switch(2), ERR_OK);
    wb_display_text(2, 2, b, sizeof b);
    CHECK_STR(b, "600");
}

/* DEMO's three sheets, which is what the old importer reported skipping. */
static void test_demo_all_three(void)
{
    char name[X16S_MAX_SHEET_NAME + 1];

    setup();
    CHECK_EQ(xlsx_import("DEMO.XLSX"), ERR_OK);
    CHECK_EQ(wb_sheet_n, 3);
    CHECK_EQ(xlsx_result()->sheets_skipped, 0);
    wb_sheet_name(0, name); CHECK_STR(name, "Budget");
    wb_sheet_name(1, name); CHECK_STR(name, "Inventory");
    wb_sheet_name(2, name); CHECK_STR(name, "Summary");
}

/* A bold heading survives the import.
 *
 * styles.xml lists its fonts once and every xf names one by index, so bold
 * is two lookups from a cell: xf -> fontId -> the <b/>. Nothing else in the
 * importer follows that chain, which is why it is worth a test of its own.
 *
 * BUDGET.XLSX has a bold heading row and plain data under it. */
static void test_import_reads_bold(void)
{
    cell_record_t rec;
    cell_style_t st;

    setup();
    CHECK_EQ(xlsx_import("BUDGET.XLSX"), ERR_OK);

    CHECK(wb_get(0, 0, &rec));                  /* A1, a heading */
    styles_get(rec.style, &st);
    CHECK(st.flags & STY_BOLD);
    CHECK(wb_bold(&rec));

    CHECK(wb_get(1, 0, &rec));                  /* A2, ordinary text */
    styles_get(rec.style, &st);
    CHECK(!(st.flags & STY_BOLD));

    /* The money column keeps its format AND stays unbold -- the bold bit
     * rides in the places byte, so a botched unpack would show up here. */
    CHECK(wb_get(1, 1, &rec));                  /* B2, currency */
    styles_get(rec.style, &st);
    CHECK_EQ(st.number_format, NF_CURRENCY);
    CHECK(!(st.flags & STY_BOLD));
    CHECK_EQ(st.decimal_places, 2);
}

void test_xlsx_import(void)
{
    test_import_reads_bold();
    test_all_sheets();
    test_formulas_land_on_their_own_sheet();
    test_demo_all_three();
    test_imported_formula_recalculates();
    test_unsupported_formula_keeps_value();
    test_demo_workbook();
    test_imported_formula_keeps_its_equals();
    test_cells_landed();
    test_not_an_xlsx();
    test_import_twice();
    test_col_widths();
    test_width_clamped();
}
