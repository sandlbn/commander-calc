/* test_sheets.c — multiple worksheets.
 *
 * The whole design rests on one thing: only the active sheet is resident,
 * and switching writes it back to banked RAM and reads the next one in. So
 * what has to be proved is that nothing leaks between sheets — cells,
 * column widths, or the used range the renderer scrolls by.
 */
#include "test.h"
#include "../src/platform/bankmem.h"
#include "../src/platform/banked_ram.h"
#include "../src/workbook/workbook.h"
#include "../src/workbook/workbook_priv.h"
#include "../src/workbook/cells.h"

static void setup(void)
{
    CHECK(bankmem_host_init(64));
    bank_heap_init(1, 0);
    CHECK_EQ(wb_init(), ERR_OK);
}

static void test_starts_with_one(void)
{
    char name[X16S_MAX_SHEET_NAME + 1];

    setup();
    CHECK_EQ(wb_sheet_n, 1);
    CHECK_EQ(wb_sheet_i, 0);
    wb_sheet_name(0, name);
    CHECK_STR(name, "Sheet1");
}

/* Cells belong to the sheet they were entered on. */
static void test_cells_stay_put(void)
{
    char b[WB_TEXT_MAX];
    cell_record_t rec;

    setup();
    CHECK_EQ(wb_set_text(0, 0, "on one"), ERR_OK);
    CHECK_EQ(wb_sheet_add("Two"), ERR_OK);
    CHECK_EQ(wb_sheet_n, 2);

    /* Adding does not switch. */
    CHECK_EQ(wb_sheet_i, 0);
    CHECK_EQ(wb_sheet_switch(1), ERR_OK);

    CHECK(!wb_get(0, 0, &rec));              /* the new sheet is empty */
    CHECK_EQ(wb_set_text(0, 0, "on two"), ERR_OK);

    CHECK_EQ(wb_sheet_switch(0), ERR_OK);
    wb_display_text(0, 0, b, sizeof b);
    CHECK_STR(b, "on one");

    CHECK_EQ(wb_sheet_switch(1), ERR_OK);
    wb_display_text(0, 0, b, sizeof b);
    CHECK_STR(b, "on two");
}

/* Column widths travel with the sheet, not with the screen. */
static void test_widths_are_per_sheet(void)
{
    setup();
    wb_set_col_width(0, 20);
    CHECK_EQ(wb_sheet_add("Two"), ERR_OK);
    CHECK_EQ(wb_sheet_switch(1), ERR_OK);

    CHECK_EQ(wb_col_width(0), X16S_DEF_COL_W);
    wb_set_col_width(0, 7);

    CHECK_EQ(wb_sheet_switch(0), ERR_OK);
    CHECK_EQ(wb_col_width(0), 20);
    CHECK_EQ(wb_sheet_switch(1), ERR_OK);
    CHECK_EQ(wb_col_width(0), 7);
}

static void test_rename(void)
{
    char name[X16S_MAX_SHEET_NAME + 1];

    setup();
    CHECK_EQ(wb_sheet_rename(0, "Budget"), ERR_OK);
    wb_sheet_name(0, name);
    CHECK_STR(name, "Budget");

    CHECK(wb_sheet_rename(0, "") != ERR_OK);        /* a sheet needs a name */
    CHECK(wb_sheet_rename(5, "nope") != ERR_OK);    /* and must exist */
}

/* Deleting shuffles the ones above down, and the active index has to follow
 * whichever way it moved. */
static void test_delete(void)
{
    char name[X16S_MAX_SHEET_NAME + 1];

    setup();
    CHECK_EQ(wb_sheet_add("Two"), ERR_OK);
    CHECK_EQ(wb_sheet_add("Three"), ERR_OK);
    CHECK_EQ(wb_sheet_n, 3);

    CHECK_EQ(wb_sheet_switch(2), ERR_OK);
    CHECK_EQ(wb_sheet_delete(0), ERR_OK);           /* below the active one */
    CHECK_EQ(wb_sheet_n, 2);
    CHECK_EQ(wb_sheet_i, 1);                 /* followed it down */
    wb_sheet_name(0, name); CHECK_STR(name, "Two");
    wb_sheet_name(1, name); CHECK_STR(name, "Three");

    CHECK_EQ(wb_sheet_delete(1), ERR_OK);           /* the active one */
    CHECK_EQ(wb_sheet_n, 1);
    CHECK_EQ(wb_sheet_i, 0);

    CHECK(wb_sheet_delete(0) != ERR_OK);            /* never the last */
}

/* Deleting a sheet must not take another sheet's cells with it. */
static void test_delete_leaves_others(void)
{
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(wb_set_text(0, 0, "keep me"), ERR_OK);
    CHECK_EQ(wb_sheet_add("Two"), ERR_OK);
    CHECK_EQ(wb_sheet_switch(1), ERR_OK);
    CHECK_EQ(wb_set_text(0, 0, "throw me"), ERR_OK);

    CHECK_EQ(wb_sheet_delete(1), ERR_OK);
    CHECK_EQ(wb_sheet_i, 0);
    wb_display_text(0, 0, b, sizeof b);
    CHECK_STR(b, "keep me");
}

static void test_limits(void)
{
    uint8_t i;

    setup();
    for (i = 1; i < X16S_MAX_SHEETS; ++i)
        CHECK_EQ(wb_sheet_add("more"), ERR_OK);
    CHECK_EQ(wb_sheet_n, X16S_MAX_SHEETS);
    CHECK(wb_sheet_add("one too many") != ERR_OK);
    CHECK(wb_sheet_switch(X16S_MAX_SHEETS) != ERR_OK);
}

/* --- cross-sheet references ------------------------------------------ *
 *
 * =Sheet2!A1. Two things have to work that did not before: the compiler has
 * to resolve a name to a sheet index, and the evaluator has to read a cell
 * that is not on the sheet it is standing on.
 */
static void test_cross_sheet_cell(void)
{
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(wb_sheet_add("Two"), ERR_OK);

    CHECK_EQ(wb_sheet_switch(1), ERR_OK);
    CHECK_EQ(wb_set_text(0, 0, "40"), ERR_OK);      /* Two!A1 = 40 */

    CHECK_EQ(wb_sheet_switch(0), ERR_OK);
    CHECK_EQ(wb_set_text(0, 0, "=Two!A1 + 2"), ERR_OK);
    wb_display_text(0, 0, b, sizeof b);
    CHECK_STR(b, "42");

    /* And the sheet we were on is still the sheet we are on: the evaluator
     * borrows the active sheet and has to give it back. */
    CHECK_EQ(wb_sheet_i, 0);
}

static void test_cross_sheet_range(void)
{
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(wb_sheet_add("Data"), ERR_OK);
    CHECK_EQ(wb_sheet_switch(1), ERR_OK);
    CHECK_EQ(wb_set_text(0, 0, "1"), ERR_OK);
    CHECK_EQ(wb_set_text(1, 0, "2"), ERR_OK);
    CHECK_EQ(wb_set_text(2, 0, "4"), ERR_OK);

    CHECK_EQ(wb_sheet_switch(0), ERR_OK);
    CHECK_EQ(wb_set_text(0, 0, "=SUM(Data!A1:A3)"), ERR_OK);
    wb_display_text(0, 0, b, sizeof b);
    CHECK_STR(b, "7");

    /* COUNT must see three values and not the whole rectangle: the empty
     * cells of a range are not zeroes. This is the path where load_cell()'s
     * return value replaced a second lookup per cell. */
    CHECK_EQ(wb_set_text(1, 0, "=COUNT(Data!A1:A9)"), ERR_OK);
    wb_display_text(1, 0, b, sizeof b);
    CHECK_STR(b, "3");
}

/* A formula on one sheet reading a formula on another. The old scheduler
 * only ever walked the active sheet, so the dependency stayed dirty and the
 * fixed point called it a circular reference. */
static void test_cross_sheet_chain(void)
{
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(wb_sheet_add("Two"), ERR_OK);

    CHECK_EQ(wb_sheet_switch(1), ERR_OK);
    CHECK_EQ(wb_set_text(0, 0, "5"), ERR_OK);          /* Two!A1  = 5      */
    CHECK_EQ(wb_set_text(1, 0, "=A1 * 3"), ERR_OK);    /* Two!A2  = 15     */

    CHECK_EQ(wb_sheet_switch(0), ERR_OK);
    CHECK_EQ(wb_set_text(0, 0, "=Two!A2 + 1"), ERR_OK);
    wb_display_text(0, 0, b, sizeof b);
    CHECK_STR(b, "16");

    /* And a change on the far sheet reaches back across. */
    CHECK_EQ(wb_sheet_switch(1), ERR_OK);
    CHECK_EQ(wb_set_text(0, 0, "10"), ERR_OK);
    CHECK_EQ(wb_sheet_switch(0), ERR_OK);
    wb_display_text(0, 0, b, sizeof b);
    CHECK_STR(b, "31");
}

static void test_cross_sheet_names(void)
{
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(wb_sheet_add("My Data"), ERR_OK);
    CHECK_EQ(wb_sheet_switch(1), ERR_OK);
    CHECK_EQ(wb_set_text(0, 0, "8"), ERR_OK);
    CHECK_EQ(wb_sheet_switch(0), ERR_OK);

    /* A name with a space has to be quoted, which is how Excel writes it
     * and therefore how it arrives from an import. */
    CHECK_EQ(wb_set_text(0, 0, "='My Data'!A1"), ERR_OK);
    wb_display_text(0, 0, b, sizeof b);
    CHECK_STR(b, "8");

    /* Naming a sheet that is not there is ERR_REF and not ERR_SYNTAX, and
     * it is refused at entry rather than stored as an error cell -- the
     * same treatment =A0 and =ZZ1 already get, see test_formula.c. */
    CHECK_EQ(wb_set_text(1, 0, "=Nowhere!A1"), ERR_REF);

    /* A plain reference still means this sheet, right after a qualified
     * one -- the prefix applies to a single reference and is then cleared. */
    CHECK_EQ(wb_set_text(2, 0, "3"), ERR_OK);
    CHECK_EQ(wb_set_text(3, 0, "='My Data'!A1 + A3"), ERR_OK);
    wb_display_text(3, 0, b, sizeof b);
    CHECK_STR(b, "11");
}

/* A sheet whose name looks like a cell reference. Without looking for the
 * '!' before trying to parse a reference, "A1!B2" reads as cell A1 followed
 * by a stray '!'. */
static void test_cross_sheet_name_like_a_cell(void)
{
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(wb_sheet_add("A1"), ERR_OK);
    CHECK_EQ(wb_sheet_switch(1), ERR_OK);
    CHECK_EQ(wb_set_text(1, 1, "99"), ERR_OK);         /* A1!B2 */
    CHECK_EQ(wb_sheet_switch(0), ERR_OK);

    CHECK_EQ(wb_set_text(0, 0, "=A1!B2"), ERR_OK);
    wb_display_text(0, 0, b, sizeof b);
    CHECK_STR(b, "99");
}

/* The sheet's state must not reach into the name that follows it.
 *
 * Both live in one 64-byte banked slot: the state at 0 and the name at
 * SH_NAME. Raising CW_MAX from six to eleven pushed the state over that
 * line, and every sheet in a saved workbook came back nameless -- no
 * error, no warning, just empty names and a file that would not reopen.
 * Nothing checked, so this does. It is the only guard on a layout two
 * headers have to agree about. */
static void test_sheet_state_fits_its_slot(void)
{
    CHECK(sizeof(sheet_state_t) <= SH_NAME);
    CHECK(SH_NAME + X16S_MAX_SHEET_NAME + 1 <= SH_SLOT);
}

void test_sheets(void)
{
    test_sheet_state_fits_its_slot();
    test_starts_with_one();
    test_cells_stay_put();
    test_widths_are_per_sheet();
    test_rename();
    test_delete();
    test_delete_leaves_others();
    test_limits();
    test_cross_sheet_cell();
    test_cross_sheet_range();
    test_cross_sheet_chain();
    test_cross_sheet_names();
    test_cross_sheet_name_like_a_cell();
}
