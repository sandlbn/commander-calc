#include "test.h"
#include "../src/formula/formula.h"
#include "../src/platform/bankmem.h"
#include "../src/platform/banked_ram.h"
#include "../src/platform/file_io.h"
#include "../src/workbook/workbook.h"
#include "../src/import/csv.h"

#define CSV "TEST.CSV"

static void setup(void)
{
    CHECK(bankmem_host_init(64));
    CHECK_EQ(bank_heap_init(1, 0), ERR_OK);
    CHECK_EQ(wb_init(), ERR_OK);
    file_host_set_root("build/host/sd");
    file_remove(CSV);
}

static void teardown(void)
{
    file_remove(CSV);
    bankmem_host_free();
}

static void write_csv(const char *text)
{
    fstream_t f;
    CHECK_EQ(file_open_write(&f, CSV), ERR_OK);
    file_write(&f, text, (uint16_t)strlen(text));
    file_close(&f);
}

static void read_back(char *out, uint16_t max)
{
    fstream_t f;
    uint16_t n;
    CHECK_EQ(file_open_read(&f, CSV), ERR_OK);
    n = file_read(&f, out, (uint16_t)(max - 1));
    out[n] = '\0';
    file_close(&f);
}

static void disp(uint16_t r, uint16_t c, char *out)
{
    wb_display_text(r, c, out, WB_TEXT_MAX);
}

/* --- import ---------------------------------------------------------- */

static void test_plain(void)
{
    csv_result_t res;
    char b[WB_TEXT_MAX];

    setup();
    write_csv("Name,Quantity,Price\nCable,4,8.50\nAdapter,2,15\n");
    CHECK_EQ(csv_import(CSV, 0, 0, &res), ERR_OK);

    CHECK_EQ(res.rows, 3);
    CHECK_EQ(res.cells, 9);
    CHECK_EQ(res.truncated, 0);

    disp(0, 0, b); CHECK_STR(b, "Name");
    disp(0, 2, b); CHECK_STR(b, "Price");
    disp(1, 0, b); CHECK_STR(b, "Cable");
    /* Type detection is the same one cell entry uses. */
    disp(1, 1, b); CHECK_STR(b, "4");
    disp(1, 2, b); CHECK_STR(b, "8.5");
    disp(2, 2, b); CHECK_STR(b, "15");

    {
        cell_record_t rec;
        CHECK_EQ(wb_get(1, 1, &rec), 1);
        CHECK_EQ(rec.type, CELL_NUMBER);
        CHECK_EQ(wb_get(1, 0, &rec), 1);
        CHECK_EQ(rec.type, CELL_TEXT);
    }
    teardown();
}

/* Every spreadsheet writes CRLF; plenty of other things write LF. Both have
 * to produce the same sheet, and neither may leave a stray row. */
static void test_line_endings(void)
{
    csv_result_t res;
    char b[WB_TEXT_MAX];

    setup();
    write_csv("a,b\r\nc,d\r\n");
    CHECK_EQ(csv_import(CSV, 0, 0, &res), ERR_OK);
    CHECK_EQ(res.rows, 2);
    disp(0, 0, b); CHECK_STR(b, "a");
    disp(1, 1, b); CHECK_STR(b, "d");
    CHECK_EQ(wb_cells()->max_row, 1);
    teardown();

    setup();
    write_csv("a,b\nc,d\n");
    CHECK_EQ(csv_import(CSV, 0, 0, &res), ERR_OK);
    CHECK_EQ(res.rows, 2);
    disp(1, 1, b); CHECK_STR(b, "d");
    teardown();

    /* No trailing newline at all — the last row must still arrive. */
    setup();
    write_csv("a,b\nc,d");
    CHECK_EQ(csv_import(CSV, 0, 0, &res), ERR_OK);
    disp(1, 1, b); CHECK_STR(b, "d");
    teardown();
}

static void test_quoting(void)
{
    csv_result_t res;
    char b[WB_TEXT_MAX];

    setup();
    write_csv("\"Smith, John\",\"say \"\"hi\"\"\",plain\n"
              "\"multi\nline\",\"\",x\n");
    CHECK_EQ(csv_import(CSV, 0, 0, &res), ERR_OK);

    /* A comma inside quotes is data, not a separator. */
    disp(0, 0, b); CHECK_STR(b, "Smith, John");
    /* A doubled quote is one quote. */
    disp(0, 1, b); CHECK_STR(b, "say \"hi\"");
    disp(0, 2, b); CHECK_STR(b, "plain");
    /* A newline inside quotes does not end the row. */
    disp(1, 0, b); CHECK_STR(b, "multi\nline");
    /* An empty quoted field leaves the cell empty rather than storing "". */
    disp(1, 1, b); CHECK_STR(b, "");
    disp(1, 2, b); CHECK_STR(b, "x");

    teardown();
}

/* Empty fields must hold their column position, or every value after a gap
 * lands in the wrong place. */
static void test_empty_fields(void)
{
    csv_result_t res;
    char b[WB_TEXT_MAX];
    cell_record_t rec;

    setup();
    write_csv("a,,c\n,,\n,x,\n");
    CHECK_EQ(csv_import(CSV, 0, 0, &res), ERR_OK);
    CHECK_EQ(res.rows, 3);

    disp(0, 0, b); CHECK_STR(b, "a");
    CHECK_EQ(wb_get(0, 1, &rec), 0);          /* genuinely empty */
    disp(0, 2, b); CHECK_STR(b, "c");
    disp(2, 1, b); CHECK_STR(b, "x");
    CHECK_EQ(wb_get(2, 0, &rec), 0);
    teardown();
}

static void test_import_at_offset(void)
{
    csv_result_t res;
    char b[WB_TEXT_MAX];

    setup();
    write_csv("a,b\nc,d\n");
    CHECK_EQ(csv_import(CSV, 10, 3, &res), ERR_OK);
    disp(10, 3, b); CHECK_STR(b, "a");
    disp(10, 4, b); CHECK_STR(b, "b");
    disp(11, 3, b); CHECK_STR(b, "c");
    disp(11, 4, b); CHECK_STR(b, "d");
    /* And nothing landed at the origin. */
    CHECK_EQ(wb_cells()->cell_count, 4);
    teardown();
}

/* Running off the right edge must be reported, not wrapped onto the next
 * row where it would silently corrupt the sheet. */
static void test_overflow_is_reported(void)
{
    csv_result_t res;

    setup();
    write_csv("a,b,c,d\n");
    CHECK_EQ(csv_import(CSV, 0, X16S_MAX_COLS - 2, &res), ERR_OK);
    CHECK_EQ(res.cells, 2);
    CHECK_EQ(res.skipped, 2);
    CHECK_EQ(res.truncated, 1);
    teardown();
}

static void test_missing_file(void)
{
    csv_result_t res;

    setup();
    CHECK_EQ(csv_import("NOSUCH.CSV", 0, 0, &res), ERR_NOTFOUND);
    teardown();
}

/* --- export ----------------------------------------------------------- */

static void test_export(void)
{
    char out[512];

    setup();
    wb_set_text(0, 0, "Name");
    wb_set_text(0, 1, "Price");
    wb_set_text(1, 0, "Cable");
    wb_set_text(1, 1, "8.5");
    CHECK_EQ(csv_export(CSV, ',', 0), ERR_OK);

    read_back(out, sizeof out);
    CHECK_STR(out, "Name,Price\r\nCable,8.5\r\n");
    teardown();
}

static void test_export_quotes_when_needed(void)
{
    char out[512];

    setup();
    wb_set_text(0, 0, "Smith, John");
    wb_set_text(0, 1, "say \"hi\"");
    wb_set_text(0, 2, "plain");
    CHECK_EQ(csv_export(CSV, ',', 0), ERR_OK);

    /* Quoted only where it matters — a file of plain values stays readable. */
    read_back(out, sizeof out);
    CHECK_STR(out, "\"Smith, John\",\"say \"\"hi\"\"\",plain\r\n");
    teardown();
}

static void test_export_formulas(void)
{
    char out[512];

    setup();
    wb_set_text(0, 0, "2");
    wb_set_text(0, 1, "=A1*3");

    /* Values by default... */
    CHECK_EQ(csv_export(CSV, ',', 0), ERR_OK);
    read_back(out, sizeof out);
    CHECK_STR(out, "2,6\r\n");

    /* ...sources on request. */
    CHECK_EQ(csv_export(CSV, ',', 1), ERR_OK);
    read_back(out, sizeof out);
    CHECK_STR(out, "2,=A1*3\r\n");
    teardown();
}

static void test_export_delimiter(void)
{
    char out[512];

    setup();
    wb_set_text(0, 0, "a");
    wb_set_text(0, 1, "b,c");
    CHECK_EQ(csv_export(CSV, ';', 0), ERR_OK);
    read_back(out, sizeof out);
    /* With a semicolon delimiter a comma is ordinary text and needs no
     * quoting — the rule follows the delimiter, not the character. */
    CHECK_STR(out, "a;b,c\r\n");
    teardown();
}

static void test_export_empty(void)
{
    char out[512];

    setup();
    CHECK_EQ(csv_export(CSV, ',', 0), ERR_OK);
    read_back(out, sizeof out);
    CHECK_STR(out, "");
    teardown();
}

/* The property that matters most: what goes out comes back. */
static void test_round_trip(void)
{
    csv_result_t res;
    char b[WB_TEXT_MAX];

    setup();
    wb_set_text(0, 0, "Name");
    wb_set_text(0, 1, "Qty");
    wb_set_text(1, 0, "Smith, John");
    wb_set_text(1, 1, "4");
    wb_set_text(2, 0, "say \"hi\"");
    wb_set_text(2, 1, "-12.5");
    CHECK_EQ(csv_export(CSV, ',', 0), ERR_OK);

    CHECK_EQ(wb_reset(), ERR_OK);
    CHECK_EQ(csv_import(CSV, 0, 0, &res), ERR_OK);

    disp(0, 0, b); CHECK_STR(b, "Name");
    disp(1, 0, b); CHECK_STR(b, "Smith, John");
    disp(1, 1, b); CHECK_STR(b, "4");
    disp(2, 0, b); CHECK_STR(b, "say \"hi\"");
    disp(2, 1, b); CHECK_STR(b, "-12.5");
    teardown();
}

/* A CSV with a formula in it.
 *
 * This crashed the machine and nothing noticed. csv_import runs in OVL_CSV
 * and called wb_set_text(), which loads OVL_FCOMPILE to compile a formula
 * -- on top of the very overlay csv_import was running in, so the call
 * returned into whatever the compiler had put at that address. It is not a
 * cross-overlay call by the usual definition, which is why check_overlays
 * missed it until it was taught about resident functions that swap.
 *
 * Formulas are collected and compiled by the caller now, the way the .xlsx
 * and .X16S readers already did it. On the host there are no overlays and
 * this could never have crashed -- what it checks is that the two-phase
 * path produces a working formula and not a lost one. */
static void test_import_with_a_formula(void)
{
    csv_result_t res;
    char b[WB_TEXT_MAX];

    setup();
    write_csv("Item,Qty,Price,Total\nCable,4,8.50,=B2*C2\nAdapter,2,15,=B3*C3\n");
    CHECK_EQ(csv_import(CSV, 0, 0, &res), ERR_OK);
    formula_compile_pending(0);   /* what the grid does next */

    disp(1, 3, b); CHECK_STR(b, "34");
    disp(2, 3, b); CHECK_STR(b, "30");

    /* Live, not the text: change an input and the total follows. */
    CHECK_EQ(wb_set_text(1, 1, "10"), ERR_OK);
    disp(1, 3, b); CHECK_STR(b, "85");

    /* And it really is a formula, not a number that happened to match. */
    wb_edit_text(1, 3, b, sizeof b);
    CHECK_STR(b, "=B2*C2");
}

void test_csv(void)
{
    test_import_with_a_formula();
    test_plain();
    test_line_endings();
    test_quoting();
    test_empty_fields();
    test_import_at_offset();
    test_overflow_is_reported();
    test_missing_file();
    test_export();
    test_export_quotes_when_needed();
    test_export_formulas();
    test_export_delimiter();
    test_export_empty();
    test_round_trip();
}
