#include "test.h"
#include "../src/import/xlsx_import.h"
#include "../src/platform/bankmem.h"
#include "../src/platform/banked_ram.h"
#include "../src/platform/file_io.h"
#include "../src/workbook/workbook.h"
#include "../src/workbook/native_file.h"
#include "../src/workbook/workbook_priv.h"   /* CW_MAX */
#include "../src/workbook/strings.h"
#include "../src/util/crc32.h"
#include <stdlib.h>

#define WB "TEST.X16S"

static void setup(void)
{
    CHECK(bankmem_host_init(64));
    CHECK_EQ(bank_heap_init(1, 0), ERR_OK);
    CHECK_EQ(wb_init(), ERR_OK);
    file_host_set_root("build/host/sd");
    file_remove(WB);
    file_remove(X16S_TMP);
}

static void teardown(void)
{
    file_remove(WB);
    file_remove(X16S_TMP);
    bankmem_host_free();
}

static void disp(uint16_t row, uint16_t col, char *out)
{
    wb_display_text(row, col, out, WB_TEXT_MAX);
}

/* Build the sheet from the spec's worked example, with a style that is not
 * the default so style ids have to survive the trip too. */
static uint8_t build_sheet(void)
{
    cell_style_t st;
    cell_record_t rec;
    uint8_t sid;
    uint16_t r, c;

    memset(&st, 0, sizeof st);
    st.number_format = NF_CURRENCY;
    st.decimal_places = 2;
    CHECK_EQ(styles_add(&st, &sid), ERR_OK);

    wb_set_text(0, 0, "Name");
    wb_set_text(0, 1, "Quantity");
    wb_set_text(0, 2, "Price");
    wb_set_text(1, 0, "Cable");
    wb_set_text(1, 1, "4");
    wb_set_text(1, 2, "8.50");
    wb_set_text(2, 0, "Adapter");
    wb_set_text(2, 1, "2");
    wb_set_text(2, 2, "15");
    wb_set_text(9999, 7, "far corner");     /* sparse, in another band */
    wb_set_text(3, 0, "TRUE");

    for (r = 1; r <= 2; ++r) {
        c = 2;
        if (wb_get(r, c, &rec)) { rec.style = sid; wb_set(r, c, &rec); }
    }
    return sid;
}

static void test_round_trip(void)
{
    char buf[WB_TEXT_MAX];
    cell_record_t rec;
    uint8_t sid;
    uint16_t cells_before, strings_before;
    uint8_t styles_before;

    setup();
    sid = build_sheet();
    cells_before   = wb_cells()->cell_count;
    strings_before = strpool_count();
    styles_before  = styles_count();

    CHECK_EQ(x16s_save(WB), ERR_OK);
    /* Saving clears modified — the workbook on disk matches the one in RAM. */
    CHECK_EQ(wb_dirty, 0);
    /* The temporary must not survive a successful save. */
    {
        fstream_t f;
        CHECK_EQ(file_open_read(&f, X16S_TMP), ERR_NOTFOUND);
    }

    /* Wipe it out, then read it back. */
    CHECK_EQ(wb_reset(), ERR_OK);
    CHECK_EQ(wb_cells()->cell_count, 0);

    CHECK_EQ(x16s_open(WB), ERR_OK);
    CHECK_EQ(wb_cells()->cell_count, cells_before);
    CHECK_EQ(strpool_count(), strings_before);
    CHECK_EQ(styles_count(), styles_before);
    CHECK_EQ(wb_dirty, 0);

    disp(0, 0, buf); CHECK_STR(buf, "Name");
    disp(1, 0, buf); CHECK_STR(buf, "Cable");
    disp(1, 1, buf); CHECK_STR(buf, "4");
    /* The currency style has to come back attached to the right cell. */
    disp(1, 2, buf); CHECK_STR(buf, "$8.50");
    disp(2, 2, buf); CHECK_STR(buf, "$15.00");
    disp(3, 0, buf); CHECK_STR(buf, "TRUE");
    disp(9999, 7, buf); CHECK_STR(buf, "far corner");

    CHECK_EQ(wb_get(1, 2, &rec), 1);
    CHECK_EQ(rec.style, sid);
    CHECK_EQ(rec.type, CELL_NUMBER);
    CHECK_EQ(wb_get(3, 0, &rec), 1);
    CHECK_EQ(rec.type, CELL_BOOLEAN);

    /* And nothing appeared that was not there. */
    CHECK_EQ(wb_get(4, 4, &rec), 0);
    CHECK_EQ(wb_cells()->max_row, 9999);

    teardown();
}

static void test_empty_workbook(void)
{
    setup();
    CHECK_EQ(x16s_save(WB), ERR_OK);
    CHECK_EQ(wb_reset(), ERR_OK);
    CHECK_EQ(x16s_open(WB), ERR_OK);
    CHECK_EQ(wb_cells()->cell_count, 0);
    teardown();
}

/* Saving twice must be idempotent — the second save replaces the first
 * cleanly rather than appending or failing because the target exists. */
static void test_resave(void)
{
    char buf[WB_TEXT_MAX];

    setup();
    build_sheet();
    CHECK_EQ(x16s_save(WB), ERR_OK);
    wb_set_text(0, 0, "Changed");
    CHECK_EQ(x16s_save(WB), ERR_OK);

    CHECK_EQ(wb_reset(), ERR_OK);
    CHECK_EQ(x16s_open(WB), ERR_OK);
    disp(0, 0, buf); CHECK_STR(buf, "Changed");
    teardown();
}

/* --- damaged files: the workbook must survive, not the file --------- */

static long saved_size(const char *name)
{
    fstream_t f;
    uint8_t buf[256];
    long n = 0;

    if (file_open_read(&f, name) != ERR_OK)
        return -1;
    for (;;) {
        uint16_t got = file_read(&f, buf, sizeof buf);
        n += got;
        if (got == 0 || file_eof(&f))
            break;
    }
    file_close(&f);
    return n;
}

/* Rewrite the file with `n` bytes removed from the end, or one byte flipped
 * at `flip` when n is 0. */
static void damage(const char *name, long truncate_by, long flip)
{
    uint8_t *data;
    long size = saved_size(name), i;
    fstream_t f;

    data = malloc((size_t)size);
    CHECK(data != 0);
    CHECK_EQ(file_open_read(&f, name), ERR_OK);
    for (i = 0; i < size; ) {
        uint16_t got = file_read(&f, data + i, 256);
        if (!got) break;
        i += got;
    }
    file_close(&f);

    if (flip >= 0 && flip < size)
        data[flip] ^= 0xFF;

    CHECK_EQ(file_open_write(&f, name), ERR_OK);
    file_write(&f, data, (uint16_t)(size - truncate_by));
    file_close(&f);
    free(data);
}

static void test_truncated_file(void)
{
    char buf[WB_TEXT_MAX];

    setup();
    build_sheet();
    CHECK_EQ(x16s_save(WB), ERR_OK);
    damage(WB, 10, -1);

    CHECK_EQ(wb_reset(), ERR_OK);
    /* An error, not a crash and not a partial workbook. */
    CHECK(x16s_open(WB) != ERR_OK);
    CHECK_EQ(wb_cells()->cell_count, 0);
    disp(0, 0, buf); CHECK_STR(buf, "");

    teardown();
}

static void test_corrupt_payload(void)
{
    setup();
    build_sheet();
    CHECK_EQ(x16s_save(WB), ERR_OK);
    /* Flip a byte well inside the data, leaving the structure intact: only
     * the checksum can catch this one. */
    damage(WB, 0, 40);

    CHECK_EQ(wb_reset(), ERR_OK);
    CHECK_EQ(x16s_open(WB), ERR_CHECKSUM);
    CHECK_EQ(wb_cells()->cell_count, 0);
    teardown();
}

static void test_not_a_workbook(void)
{
    fstream_t f;

    setup();
    CHECK_EQ(file_open_write(&f, WB), ERR_OK);
    file_write(&f, "this is not a workbook at all", 29);
    file_close(&f);

    wb_set_text(0, 0, "keep me");
    CHECK_EQ(x16s_open(WB), ERR_BADFORMAT);
    /* A rejected header is detected before anything is discarded, so the
     * workbook the user was working on is still there. */
    {
        char buf[WB_TEXT_MAX];
        disp(0, 0, buf);
        CHECK_STR(buf, "keep me");
    }
    teardown();
}

static void test_missing_file(void)
{
    setup();
    CHECK_EQ(x16s_open("NOSUCH.X16S"), ERR_NOTFOUND);
    teardown();
}

/* A file from a future version with a section we do not know must load, not
 * fail — that is the whole reason the format is chunked. */
static void test_unknown_chunk_is_skipped(void)
{
    char buf[WB_TEXT_MAX];
    uint8_t *data;
    long size, i, insert_at = 8;   /* right after magic+version+flags */
    fstream_t f;
    static const uint8_t chunk[] = {
        'C','H','R','T', 6,0,0,0, 1,2,3,4,5,6
    };
    uint32_t crc;

    setup();
    build_sheet();
    CHECK_EQ(x16s_save(WB), ERR_OK);

    size = saved_size(WB);
    data = malloc((size_t)size + sizeof chunk);
    CHECK(data != 0);
    CHECK_EQ(file_open_read(&f, WB), ERR_OK);
    for (i = 0; i < size; ) {
        uint16_t got = file_read(&f, data + i, 256);
        if (!got) break;
        i += got;
    }
    file_close(&f);

    memmove(data + insert_at + sizeof chunk, data + insert_at,
            (size_t)(size - insert_at));
    memcpy(data + insert_at, chunk, sizeof chunk);
    size += sizeof chunk;

    /* The checksum covers the new chunk too, so recompute it. */
    crc = crc32_final(crc32_update(crc32_init(), data, (uint16_t)(size - 4)));
    data[size - 4] = (uint8_t)crc;
    data[size - 3] = (uint8_t)(crc >> 8);
    data[size - 2] = (uint8_t)(crc >> 16);
    data[size - 1] = (uint8_t)(crc >> 24);

    CHECK_EQ(file_open_write(&f, WB), ERR_OK);
    file_write(&f, data, (uint16_t)size);
    file_close(&f);
    free(data);

    CHECK_EQ(wb_reset(), ERR_OK);
    CHECK_EQ(x16s_open(WB), ERR_OK);
    disp(1, 0, buf); CHECK_STR(buf, "Cable");
    disp(1, 2, buf); CHECK_STR(buf, "$8.50");

    teardown();
}

/* Column widths survive a save and reload.
 *
 * They are their own chunk, so this also checks the skip path stays honest:
 * a reader that did not know COLW has to step over it and still reach the
 * checksum, which is what lets a file move between versions. */
static void test_col_widths_roundtrip(void)
{
    setup();
    CHECK_EQ(wb_set_text(0, 0, "keep"), ERR_OK);
    wb_set_col_width(0, 18);
    wb_set_col_width(7, X16S_MIN_COL_W);

    CHECK_EQ(x16s_save("WIDTH.X16S"), ERR_OK);
    CHECK_EQ(wb_reset(), ERR_OK);
    CHECK_EQ(wb_col_width(0), X16S_DEF_COL_W);      /* really gone */

    CHECK_EQ(x16s_open("WIDTH.X16S"), ERR_OK);
    CHECK_EQ(wb_col_width(0), 18);
    CHECK_EQ(wb_col_width(7), X16S_MIN_COL_W);
    CHECK_EQ(wb_col_width(3), X16S_DEF_COL_W);
}

/* As many as a sheet can hold, round-tripped.
 *
 * A sheet keeps CW_MAX non-default widths and drops the next in silence,
 * so this pins what that number actually is -- it was six, which is fewer
 * than a person setting up a table would use before noticing that one of
 * them would not stick. */
static void test_all_column_widths_round_trip(void)
{
    uint8_t c;

    setup();
    CHECK_EQ(wb_set_text(0, 0, "keep"), ERR_OK);
    for (c = 0; c < CW_MAX; ++c)
        wb_set_col_width(c, (uint8_t)(12 + c));

    CHECK_EQ(x16s_save("WIDTHS.X16S"), ERR_OK);
    CHECK_EQ(wb_reset(), ERR_OK);
    CHECK_EQ(x16s_open("WIDTHS.X16S"), ERR_OK);

    for (c = 0; c < CW_MAX; ++c)
        CHECK_EQ(wb_col_width(c), (uint8_t)(12 + c));
}

/* Every sheet, not just the one on screen.
 *
 * Saving used to write wb_cells() -- the active sheet -- and a hardcoded
 * sheet count of 1. A three-sheet workbook saved and reloaded came back
 * with two of them gone, silently, in the program's own format. */
static void test_sheets_round_trip(void)
{
    char b[WB_TEXT_MAX];
    char name[X16S_MAX_SHEET_NAME + 1];

    setup();
    CHECK_EQ(wb_set_text(0, 0, "on one"), ERR_OK);
    wb_set_col_width(0, 21);

    CHECK_EQ(wb_sheet_add("Middle"), ERR_OK);
    CHECK_EQ(wb_sheet_switch(1), ERR_OK);
    CHECK_EQ(wb_set_text(3, 2, "on two"), ERR_OK);

    CHECK_EQ(wb_sheet_add("Last"), ERR_OK);
    CHECK_EQ(wb_sheet_switch(2), ERR_OK);
    CHECK_EQ(wb_set_text(9, 0, "on three"), ERR_OK);
    wb_set_col_width(0, 7);

    CHECK_EQ(wb_sheet_switch(1), ERR_OK);       /* saved while on the middle */
    CHECK_EQ(x16s_save("SHEETS.X16S"), ERR_OK);
    CHECK_EQ(wb_reset(), ERR_OK);
    CHECK_EQ(wb_sheet_n, 1);

    CHECK_EQ(x16s_open("SHEETS.X16S"), ERR_OK);
    CHECK_EQ(wb_sheet_n, 3);

    CHECK_EQ(wb_sheet_switch(0), ERR_OK);
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "on one");
    CHECK_EQ(wb_col_width(0), 21);

    CHECK_EQ(wb_sheet_switch(1), ERR_OK);
    wb_sheet_name(1, name); CHECK_STR(name, "Middle");
    wb_display_text(3, 2, b, sizeof b); CHECK_STR(b, "on two");

    CHECK_EQ(wb_sheet_switch(2), ERR_OK);
    wb_sheet_name(2, name); CHECK_STR(name, "Last");
    wb_display_text(9, 0, b, sizeof b); CHECK_STR(b, "on three");
    CHECK_EQ(wb_col_width(0), 7);
}

/* A cross-sheet formula through a save and a load.
 *
 * Worth its own test because =Sheet2!A1 compiles to a sheet INDEX, not a
 * name: the bytecode says "sheet 1" and means whichever sheet is second
 * when it runs. So the round trip has to preserve sheet order, and the
 * formula has to still be pointing at the right one afterwards -- a reader
 * that restored the sheets in a different order would leave every
 * cross-sheet formula quietly reading the wrong cell rather than failing. */
/* Probe: a plain, same-sheet formula through save and load. */
static void test_plain_formula_round_trip(void)
{
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(wb_set_text(0, 0, "6"), ERR_OK);
    CHECK_EQ(wb_set_text(1, 0, "7"), ERR_OK);
    CHECK_EQ(wb_set_text(2, 0, "=A1+A2"), ERR_OK);
    wb_display_text(2, 0, b, sizeof b);
    CHECK_STR(b, "13");

    CHECK_EQ(x16s_save("PLAINF.X16S"), ERR_OK);
    CHECK_EQ(wb_reset(), ERR_OK);
    CHECK_EQ(x16s_open("PLAINF.X16S"), ERR_OK);

    wb_display_text(2, 0, b, sizeof b);
    CHECK_STR(b, "13");
}

static void test_cross_sheet_round_trip(void)
{
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(wb_sheet_add("Two"), ERR_OK);
    CHECK_EQ(wb_sheet_switch(1), ERR_OK);
    CHECK_EQ(wb_set_text(0, 0, "6"), ERR_OK);
    CHECK_EQ(wb_set_text(1, 0, "7"), ERR_OK);

    CHECK_EQ(wb_sheet_switch(0), ERR_OK);
    CHECK_EQ(wb_set_text(0, 0, "=SUM(Two!A1:A2) * 2"), ERR_OK);
    wb_display_text(0, 0, b, sizeof b);
    CHECK_STR(b, "26");

    CHECK_EQ(x16s_save("XSHEET.X16S"), ERR_OK);
    CHECK_EQ(wb_reset(), ERR_OK);
    CHECK_EQ(x16s_open("XSHEET.X16S"), ERR_OK);

    CHECK_EQ(wb_sheet_n, 2);
    CHECK_EQ(wb_sheet_switch(0), ERR_OK);
    wb_display_text(0, 0, b, sizeof b);
    CHECK_STR(b, "26");

    /* And it is still live: changing the far sheet moves it. */
    CHECK_EQ(wb_sheet_switch(1), ERR_OK);
    CHECK_EQ(wb_set_text(0, 0, "10"), ERR_OK);
    CHECK_EQ(wb_sheet_switch(0), ERR_OK);
    wb_display_text(0, 0, b, sizeof b);
    CHECK_STR(b, "34");
}

/* The same, but with a workbook the program did not build itself.
 *
 * test_sheets_round_trip() passed while this failed: three sheets typed in
 * here leave a tidy string pool, and three sheets imported from DEMO.XLSX
 * do not. write_strings() was writing strpool_count() as the number of
 * records while measuring the payload from every alive id, and when those
 * disagreed the reader stopped early and read the rest of the file as
 * garbage -- ERR_EOF, several chunks later, with nothing pointing at the
 * cause. A round trip of real data is what finds that. */
static void test_imported_workbook_round_trip(void)
{
    char b[WB_TEXT_MAX];
    char name[X16S_MAX_SHEET_NAME + 1];

    setup();
    CHECK_EQ(xlsx_import("DEMO.XLSX"), ERR_OK);
    CHECK_EQ(wb_sheet_n, 3);

    CHECK_EQ(x16s_save("DEMORT.X16S"), ERR_OK);
    CHECK_EQ(wb_reset(), ERR_OK);
    CHECK_EQ(x16s_open("DEMORT.X16S"), ERR_OK);

    CHECK_EQ(wb_sheet_n, 3);
    wb_sheet_name(0, name); CHECK_STR(name, "Budget");
    wb_sheet_name(1, name); CHECK_STR(name, "Inventory");
    wb_sheet_name(2, name); CHECK_STR(name, "Summary");

    /* Contents, not just names: a shared string on the first sheet and one
     * on the third, either side of the chunk that went wrong. */
    CHECK_EQ(wb_sheet_switch(0), ERR_OK);
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "Category");
    CHECK_EQ(wb_col_width(0), 18);

    CHECK_EQ(wb_sheet_switch(2), ERR_OK);
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "Summary");
}

/* No id claims to be alive with an impossible length.
 *
 * This is the assertion that would have caught the free-list bug on the
 * host, where bank 0 is zeroed and the corruption was otherwise invisible:
 * the *count* of alive ids was wrong even when their lengths looked
 * harmless. DEMO.XLSX is the workbook that exposes it, because more than
 * one of its formulas is one this program cannot compile, and each of
 * those frees the source string it was holding. */
static void test_pool_sane_after_import(void)
{
    uint16_t id, max, alive = 0, bad = 0;

    setup();
    CHECK_EQ(xlsx_import("DEMO.XLSX"), ERR_OK);

    max = strpool_max_id();
    for (id = 1; id <= max; ++id)
        if (strpool_alive(id)) {
            ++alive;
            if (strpool_len(id) > X16S_MAX_TEXT_LEN)
                ++bad;
        }
    CHECK_EQ(bad, 0);
    CHECK(alive > 0);
    CHECK(alive <= max);
}

void test_x16s(void)
{
    test_pool_sane_after_import();
    test_sheets_round_trip();
    test_plain_formula_round_trip();
    test_cross_sheet_round_trip();
    test_imported_workbook_round_trip();
    test_col_widths_roundtrip();
    test_all_column_widths_round_trip();
    test_round_trip();
    test_empty_workbook();
    test_resave();
    test_truncated_file();
    test_corrupt_payload();
    test_not_a_workbook();
    test_missing_file();
    test_unknown_chunk_is_skipped();
}
