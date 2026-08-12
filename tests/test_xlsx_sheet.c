#include "test.h"
#include "../src/platform/bankmem.h"
#include "../src/platform/banked_ram.h"
#include "../src/platform/file_io.h"
#include "../src/workbook/workbook.h"
#include "../src/workbook/strings.h"
#include "../src/workbook/styles.h"
#include "../src/util/date.h"
#include "../src/import/blob.h"
#include "../src/import/inflate.h"
#include "../src/import/xlsx.h"
#include "../src/import/xlsx_sheet.h"
#include "../src/import/xlsx_stage.h"
#include "../src/platform/banked_ram.h"

/* --- dates ------------------------------------------------------------ */

/* Excel's calendar is wrong on purpose and every file agrees with it, so
 * these values are what the format means rather than what a calendar says. */
static void test_serial_dates(void)
{
    uint16_t y;
    uint8_t m, d;
    char buf[12];

    date_from_serial(1, &y, &m, &d);
    CHECK_EQ(y, 1900); CHECK_EQ(m, 1); CHECK_EQ(d, 1);

    /* The last correct day before the phantom one. */
    date_from_serial(59, &y, &m, &d);
    CHECK_EQ(y, 1900); CHECK_EQ(m, 2); CHECK_EQ(d, 28);

    /* Serial 60 is 29 February 1900, a day that did not happen. Excel
     * believes in it, so a file that says 60 means this. */
    date_from_serial(60, &y, &m, &d);
    CHECK_EQ(y, 1900); CHECK_EQ(m, 2); CHECK_EQ(d, 29);

    /* And everything after it is offset by one, which is why the correction
     * cannot simply be ignored. */
    date_from_serial(61, &y, &m, &d);
    CHECK_EQ(y, 1900); CHECK_EQ(m, 3); CHECK_EQ(d, 1);

    /* Values anyone would recognise. */
    date_from_serial(25569, &y, &m, &d);            /* the Unix epoch */
    CHECK_EQ(y, 1970); CHECK_EQ(m, 1); CHECK_EQ(d, 1);
    date_from_serial(36526, &y, &m, &d);
    CHECK_EQ(y, 2000); CHECK_EQ(m, 1); CHECK_EQ(d, 1);
    date_from_serial(45658, &y, &m, &d);
    CHECK_EQ(y, 2025); CHECK_EQ(m, 1); CHECK_EQ(d, 1);

    /* 2000 was a leap year, 1900 was not — the rule that catches naive
     * implementations in both directions. */
    date_from_serial(36585, &y, &m, &d);
    CHECK_EQ(y, 2000); CHECK_EQ(m, 2); CHECK_EQ(d, 29);

    date_format(45658, buf);
    CHECK_STR(buf, "2025-01-01");
    date_format(1, buf);
    CHECK_STR(buf, "1900-01-01");
}

/* Round-tripping is the real check: any offset error shows up immediately. */
static void test_serial_roundtrip(void)
{
    uint32_t s;
    uint16_t y;
    uint8_t m, d;

    for (s = 1; s < 50000; s += 137) {
        date_from_serial(s, &y, &m, &d);
        if (s == 60)
            continue;               /* not a real date, so not invertible */
        if (date_to_serial(y, m, d) != s) {
            CHECK_EQ(date_to_serial(y, m, d), s);
            return;
        }
    }
    CHECK(1);
}

static void test_time_of_day(void)
{
    char buf[10];
    uint8_t h, m, s;

    date_time_from_fraction(0, &h, &m, &s);
    CHECK_EQ(h, 0); CHECK_EQ(m, 0); CHECK_EQ(s, 0);

    /* Half a day is noon, and must not come out as 11:59:59. */
    date_time_from_fraction(500, &h, &m, &s);
    CHECK_EQ(h, 12); CHECK_EQ(m, 0); CHECK_EQ(s, 0);

    date_format_time(750, buf);
    CHECK_STR(buf, "18:00:00");
    date_format_time(999, buf);
    CHECK_STR(buf, "23:58:34");
}

/* --- the demo workbook, through the whole pipeline -------------------- */

static blob_t part;
static xlsx_target_t tgt;
static handle_t st;
static xlsx_styles_info_t sinfo;
static handle_t ids;

static uint16_t nstrings;

static void setup(void)
{
    CHECK(bankmem_host_init(64));
    CHECK_EQ(bank_heap_init(1, 0), ERR_OK);
    CHECK_EQ(wb_init(), ERR_OK);
    file_host_set_root("build/host/sd");
    memset(&part, 0, sizeof part);
    ids = bank_alloc(2048);
    st = bank_calloc(XLSX_STYLE_BYTES);
}

static err_t fetch(const char *path)
{
    stage_info_t info;
    static blob_t raw;
    err_t e = xlsx_stage("DEMO.XLSX", path, &raw, &info);

    if (e != ERR_OK)
        return e;
    if (!info.compressed) {
        part = raw;
        memset(&raw, 0, sizeof raw);
        return ERR_OK;
    }
    return inflate_blob(&raw, &part, info.raw_size);
}

/* Everything up to the point where worksheet `n` can be read: exactly the
 * sequence the driver runs, in the same order. */
static err_t open_demo(uint8_t n)
{
    err_t e = fetch("xl/workbook.xml");
    if (e != ERR_OK) return e;
    e = xlsx_find_sheet(&part, n, &tgt);
    if (e != ERR_OK) return e;
    e = fetch("xl/_rels/workbook.xml.rels");
    if (e != ERR_OK) return e;
    e = xlsx_find_path(&part, &tgt);
    if (e != ERR_OK) return e;
    e = fetch("xl/sharedStrings.xml");
    if (e != ERR_OK) return e;
    xlsx_parse_strings(&part, ids, 1024, &nstrings, 0);
    e = fetch("xl/styles.xml");
    if (e != ERR_OK) return e;
    return xlsx_parse_styles(&part, st, &sinfo);
}

static void disp(uint16_t r, uint16_t c, char *out)
{
    wb_display_text(r, c, out, WB_TEXT_MAX);
}

/* --- styles ----------------------------------------------------------- */

static void test_styles(void)
{
    setup();
    CHECK_EQ(open_demo(0), ERR_OK);

    /* The demo declares seven cell formats, in this order:
     * General, currency, percent, date, integer, two-decimal, bold text. */
    CHECK_EQ(sinfo.count, 7);
    CHECK_EQ(bank_peek(st, XLSX_STYLE_FMT(0)), NF_GENERAL);
    CHECK_EQ(bank_peek(st, XLSX_STYLE_FMT(1)), NF_CURRENCY); /* "$"#,##0.00 */
    CHECK_EQ(bank_peek(st, XLSX_STYLE_FMT(2)), NF_PERCENT);  /* builtin 9   */
    CHECK_EQ(bank_peek(st, XLSX_STYLE_FMT(3)), NF_DATE);     /* yyyy-mm-dd  */
    CHECK_EQ(bank_peek(st, XLSX_STYLE_FMT(4)), NF_INTEGER);  /* builtin 1   */
    CHECK_EQ(bank_peek(st, XLSX_STYLE_FMT(5)), NF_DECIMAL);  /* builtin 2   */
    CHECK_EQ(bank_peek(st, XLSX_STYLE_FMT(6)), NF_GENERAL);  /* bold        */
    CHECK_EQ(sinfo.dropped, 0);

    /* The decimal count comes out of the code, not a guess. */
    CHECK_EQ(bank_peek(st, XLSX_STYLE_PLACES(1)), 2);

    bankmem_host_free();
}

/* --- the budget sheet -------------------------------------------------- */

static void test_budget_sheet(void)
{
    xlsx_sheet_result_t res;
    char b[WB_TEXT_MAX];
    cell_record_t rec;

    setup();
    CHECK_EQ(open_demo(0), ERR_OK);
    CHECK_EQ(fetch(tgt.u.path), ERR_OK);
    CHECK_EQ(xlsx_parse_sheet(&part, ids, nstrings, st, sinfo.count, &res), ERR_OK);

    CHECK(res.cells > 80);
    CHECK_EQ(res.truncated, 0);
    CHECK(res.formulas > 0);
    CHECK(res.dates > 0);

    /* Header row: shared strings resolved through the index map. */
    disp(0, 0, b); CHECK_STR(b, "Category");
    disp(0, 1, b); CHECK_STR(b, "Budgeted");
    disp(0, 6, b); CHECK_STR(b, "Over?");

    /* A shared string in the body. */
    disp(1, 0, b); CHECK_STR(b, "Rent");

    /* Currency: the value stays a plain number and the style is what puts
     * the symbol and the two decimals on it. */
    CHECK_EQ(wb_get(1, 1, &rec), 1);
    CHECK_EQ(rec.type, CELL_NUMBER);
    disp(1, 1, b); CHECK_STR(b, "$1200.00");

    /* A date cell is a number the style turned into a date. */
    CHECK_EQ(wb_get(1, 5, &rec), 1);
    CHECK_EQ(rec.type, CELL_DATE);
    disp(1, 5, b); CHECK_STR(b, "2026-01-01");

    /* A percentage. */
    disp(1, 4, b); CHECK_STR(b, "35%");

    /* A boolean from t="b". */
    CHECK_EQ(wb_get(1, 6, &rec), 1);
    CHECK_EQ(rec.type, CELL_BOOLEAN);

    /* A formula cell holds Excel's cached result, which is what we can
     * honestly show before the formula engine has looked at it. */
    CHECK_EQ(wb_get(13, 1, &rec), 1);           /* the SUM row */
    disp(13, 1, b); CHECK_STR(b, "$3531.18");   /* the twelve budget lines */

    bankmem_host_free();
}

/* The cases a real file contains that a parser has to survive rather than
 * understand. */
static void test_edge_cells(void)
{
    xlsx_sheet_result_t res;
    char b[WB_TEXT_MAX];
    cell_record_t rec;

    setup();
    CHECK_EQ(open_demo(0), ERR_OK);
    CHECK_EQ(fetch(tgt.u.path), ERR_OK);
    CHECK_EQ(xlsx_parse_sheet(&part, ids, nstrings, st, sinfo.count, &res), ERR_OK);

    /* An error Excel already had, kept as an error rather than as text. */
    CHECK_EQ(wb_get(17, 1, &rec), 1);
    CHECK_EQ(rec.type, CELL_ERROR);
    disp(17, 1, b); CHECK_STR(b, "#DIV/0!");

    /* t="inlineStr": the text is inside the cell, not in the shared table. */
    CHECK_EQ(wb_get(17, 3, &rec), 1);
    CHECK_EQ(rec.type, CELL_TEXT);
    disp(17, 3, b); CHECK_STR(b, "inline text");

    /* A negative with two decimals. */
    disp(17, 6, b); CHECK_STR(b, "-0.50");

    bankmem_host_free();
}

/* 200 rows through the whole pipeline: the part that is 52 KB of XML. */
static void test_large_sheet(void)
{
    xlsx_sheet_result_t res;
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(open_demo(1), ERR_OK);
    CHECK_EQ(fetch(tgt.u.path), ERR_OK);
    CHECK_EQ(xlsx_parse_sheet(&part, ids, nstrings, st, sinfo.count, &res), ERR_OK);

    /* 6 header + 200 * 6 + 2 in the total row, minus the empty ones. */
    CHECK(res.cells > 1100);
    CHECK_EQ(res.truncated, 0);

    disp(0, 0, b);   CHECK_STR(b, "SKU");
    disp(1, 0, b);   CHECK_STR(b, "SKU-1000");     /* inlineStr */
    disp(1, 1, b);   CHECK_STR(b, "Cable");        /* shared string */
    disp(1, 2, b);   CHECK_STR(b, "0");            /* integer format */
    disp(200, 0, b); CHECK_STR(b, "SKU-1199");     /* the last row */

    CHECK_EQ(wb_cells()->max_row, 201);

    bankmem_host_free();
}

/* A sheet must be importable with no styles at all: a workbook that never
 * formatted anything has no styles.xml, and every number is then General. */
static void test_no_styles(void)
{
    xlsx_sheet_result_t res;
    cell_record_t rec;

    setup();
    CHECK_EQ(open_demo(0), ERR_OK);
    CHECK_EQ(fetch(tgt.u.path), ERR_OK);
    CHECK_EQ(xlsx_parse_sheet(&part, ids, nstrings, st, 0, &res),
             ERR_OK);

    /* Without a style saying so, a date serial stays a number. */
    CHECK_EQ(wb_get(1, 5, &rec), 1);
    CHECK_EQ(rec.type, CELL_NUMBER);
    CHECK_EQ(res.dates, 0);

    bankmem_host_free();
}

/* Cells out of the range this program models are counted and dropped, not
 * wrapped into the wrong place. */
static void test_reference_parsing(void)
{
    setup();
    CHECK_EQ(open_demo(0), ERR_OK);
    /* Column letters are bijective base 26, the same as the grid's own
     * naming — A is 1, so IV is the 256th and last we hold. */
    {
        xlsx_sheet_result_t res;
        CHECK_EQ(fetch(tgt.u.path), ERR_OK);
        CHECK_EQ(xlsx_parse_sheet(&part, ids, nstrings, st, sinfo.count, &res),
                 ERR_OK);
        CHECK(res.cells > 0);
        CHECK_EQ(res.truncated, 0);
    }
    bankmem_host_free();
}

/* Shared strings are parsed once for the workbook and then used by every
 * sheet. Importing a second sheet must still resolve them.
 *
 * This is a regression test with a story: the string count lived in the
 * per-sheet struct, which xlsx_find_sheet resets, so the count read as zero
 * from the second sheet onward and every shared-string cell was silently
 * skipped. Nothing failed — the sheet simply came in 200 cells short. */
static void test_strings_survive_second_sheet(void)
{
    xlsx_sheet_result_t r0, r1;
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(open_demo(0), ERR_OK);
    CHECK_EQ(fetch(tgt.u.path), ERR_OK);
    CHECK_EQ(xlsx_parse_sheet(&part, ids, nstrings, st, sinfo.count, &r0),
             ERR_OK);

    /* Now the second sheet, WITHOUT re-reading sharedStrings.xml — which is
     * what the driver does, and what the earlier tests happened not to. */
    CHECK_EQ(fetch("xl/workbook.xml"), ERR_OK);
    CHECK_EQ(xlsx_find_sheet(&part, 1, &tgt), ERR_OK);
    CHECK_EQ(fetch("xl/_rels/workbook.xml.rels"), ERR_OK);
    CHECK_EQ(xlsx_find_path(&part, &tgt), ERR_OK);
    CHECK_EQ(fetch(tgt.u.path), ERR_OK);
    CHECK_EQ(xlsx_parse_sheet(&part, ids, nstrings, st, sinfo.count, &r1),
             ERR_OK);

    /* The full count, not the count minus every shared-string cell. */
    CHECK_EQ(r1.cells, 1208);
    CHECK_EQ(r1.truncated, 0);
    disp(1, 1, b); CHECK_STR(b, "Cable");    /* a shared string, resolved */

    bankmem_host_free();
}

void test_xlsx_sheet(void)
{
    test_serial_dates();
    test_serial_roundtrip();
    test_time_of_day();
    test_styles();
    test_budget_sheet();
    test_edge_cells();
    test_large_sheet();
    test_no_styles();
    test_reference_parsing();
    test_strings_survive_second_sheet();
}
