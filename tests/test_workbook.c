#include "test.h"
#include "../src/platform/bankmem.h"
#include "../src/platform/banked_ram.h"
#include "../src/workbook/workbook.h"
#include "../src/workbook/strings.h"

static void setup(void)
{
    CHECK(bankmem_host_init(64));
    CHECK_EQ(bank_heap_init(1, 0), ERR_OK);
    CHECK_EQ(wb_init(), ERR_OK);
}

static void disp(uint16_t row, uint16_t col, char *out)
{
    wb_display_text(row, col, out, WB_TEXT_MAX);
}

/* --- strings -------------------------------------------------------- */

static void test_strpool(void)
{
    uint16_t a, b;
    char buf[32];

    setup();
    CHECK_EQ(strpool_count(), 0);

    CHECK_EQ(strpool_add("Cable", 5, &a), ERR_OK);
    CHECK(a != STR_NONE);
    CHECK_EQ(strpool_count(), 1);
    CHECK_EQ(strpool_get(a, buf, sizeof buf), 5);
    CHECK_STR(buf, "Cable");
    CHECK_EQ(strpool_len(a), 5);

    CHECK_EQ(strpool_add("Adapter", 7, &b), ERR_OK);
    CHECK(b != a);
    CHECK_EQ(strpool_get(b, buf, sizeof buf), 7);
    CHECK_STR(buf, "Adapter");

    /* Truncation must still terminate, and must report the true length so
     * the caller can tell it was cut. */
    CHECK_EQ(strpool_get(b, buf, 4), 7);
    CHECK_STR(buf, "Ada");

    /* The empty string is a legitimate value, not an error. */
    {
        uint16_t e;
        CHECK_EQ(strpool_add("", 0, &e), ERR_OK);
        CHECK_EQ(strpool_get(e, buf, sizeof buf), 0);
        CHECK_STR(buf, "");
    }

    /* Reading a dead id is safe and yields nothing. */
    CHECK_EQ(strpool_get(STR_NONE, buf, sizeof buf), 0);
    CHECK_EQ(strpool_get(9999, buf, sizeof buf), 0);

    bankmem_host_free();
}

static void test_strpool_refcount(void)
{
    uint16_t id, id2;
    char buf[32];

    setup();
    CHECK_EQ(strpool_add("shared", 6, &id), ERR_OK);
    strpool_ref(id);                       /* two owners */
    strpool_unref(id);                     /* one leaves */
    CHECK_EQ(strpool_count(), 1);
    CHECK_EQ(strpool_get(id, buf, sizeof buf), 6);   /* still alive */

    strpool_unref(id);                     /* last owner leaves */
    CHECK_EQ(strpool_count(), 0);

    /* The id is recycled rather than burned — otherwise repeated editing
     * would exhaust the 8192-id space. */
    CHECK_EQ(strpool_add("again", 5, &id2), ERR_OK);
    CHECK_EQ(id2, id);
    CHECK_EQ(strpool_count(), 1);

    bankmem_host_free();
}

/* --- type detection on entry ---------------------------------------- */

static void test_type_detection(void)
{
    cell_record_t rec;
    char buf[WB_TEXT_MAX];

    setup();

    wb_set_text(0, 0, "Cable");
    CHECK_EQ(wb_get(0, 0, &rec), 1);
    CHECK_EQ(rec.type, CELL_TEXT);
    disp(0, 0, buf); CHECK_STR(buf, "Cable");

    wb_set_text(0, 1, "4");
    CHECK_EQ(wb_get(0, 1, &rec), 1);
    CHECK_EQ(rec.type, CELL_NUMBER);
    disp(0, 1, buf); CHECK_STR(buf, "4");

    wb_set_text(0, 2, "8.50");
    CHECK_EQ(wb_get(0, 2, &rec), 1);
    CHECK_EQ(rec.type, CELL_NUMBER);
    disp(0, 2, buf); CHECK_STR(buf, "8.5");   /* General drops the zero */

    wb_set_text(0, 3, "-12.5");
    CHECK_EQ(wb_get(0, 3, &rec), 1);
    CHECK_EQ(rec.type, CELL_NUMBER);
    disp(0, 3, buf); CHECK_STR(buf, "-12.5");

    wb_set_text(0, 4, "TRUE");
    CHECK_EQ(wb_get(0, 4, &rec), 1);
    CHECK_EQ(rec.type, CELL_BOOLEAN);
    disp(0, 4, buf); CHECK_STR(buf, "TRUE");

    wb_set_text(0, 5, "false");             /* case-insensitive */
    CHECK_EQ(wb_get(0, 5, &rec), 1);
    CHECK_EQ(rec.type, CELL_BOOLEAN);
    disp(0, 5, buf); CHECK_STR(buf, "FALSE");

    /* Things that merely look numeric are text. A phone number or a part
     * code must not be silently mangled into a float. */
    wb_set_text(1, 0, "1.2.3");
    CHECK_EQ(wb_get(1, 0, &rec), 1);
    CHECK_EQ(rec.type, CELL_TEXT);
    disp(1, 0, buf); CHECK_STR(buf, "1.2.3");

    wb_set_text(1, 1, "12A");
    CHECK_EQ(wb_get(1, 1, &rec), 1);
    CHECK_EQ(rec.type, CELL_TEXT);

    /* Empty input clears rather than storing an empty cell. */
    wb_set_text(0, 0, "");
    CHECK_EQ(wb_get(0, 0, &rec), 0);

    bankmem_host_free();
}

static void test_alignment(void)
{
    cell_record_t rec;

    setup();
    wb_set_text(0, 0, "Cable");
    wb_get(0, 0, &rec);
    CHECK_EQ(wb_align_right(&rec), 0);     /* text left */

    wb_set_text(0, 1, "42");
    wb_get(0, 1, &rec);
    CHECK_EQ(wb_align_right(&rec), 1);     /* numbers right */

    bankmem_host_free();
}

/* --- number formats -------------------------------------------------- */

static void test_number_formats(void)
{
    cell_record_t rec;
    cell_style_t st;
    char buf[WB_TEXT_MAX];
    uint8_t id;

    setup();

    memset(&st, 0, sizeof st);
    st.number_format = NF_CURRENCY;
    st.decimal_places = 2;
    CHECK_EQ(styles_add(&st, &id), ERR_OK);

    wb_set_text(0, 0, "8.5");
    wb_get(0, 0, &rec);
    rec.style = id;
    wb_set(0, 0, &rec);
    disp(0, 0, buf); CHECK_STR(buf, "$8.50");

    st.number_format = NF_PERCENT;
    st.decimal_places = 1;
    CHECK_EQ(styles_add(&st, &id), ERR_OK);
    wb_set_text(0, 1, "0.125");
    wb_get(0, 1, &rec);
    rec.style = id;
    wb_set(0, 1, &rec);
    disp(0, 1, buf); CHECK_STR(buf, "12.5%");

    st.number_format = NF_INTEGER;
    CHECK_EQ(styles_add(&st, &id), ERR_OK);
    wb_set_text(0, 2, "3.7");
    wb_get(0, 2, &rec);
    rec.style = id;
    wb_set(0, 2, &rec);
    disp(0, 2, buf); CHECK_STR(buf, "4");     /* rounds, not truncates */

    /* The edit view is unformatted: what the user typed, not what is shown. */
    wb_edit_text(0, 0, buf, sizeof buf);
    CHECK_STR(buf, "8.5");

    bankmem_host_free();
}

static void test_style_sharing(void)
{
    cell_style_t st;
    uint8_t a, b;

    setup();
    CHECK_EQ(styles_count(), 1);           /* style 0 = General, always */

    memset(&st, 0, sizeof st);
    st.number_format = NF_CURRENCY;
    st.decimal_places = 2;
    CHECK_EQ(styles_add(&st, &a), ERR_OK);
    CHECK_EQ(styles_count(), 2);

    /* An identical style must reuse the slot — a currency column should not
     * consume one of 128 entries per cell. */
    CHECK_EQ(styles_add(&st, &b), ERR_OK);
    CHECK_EQ(b, a);
    CHECK_EQ(styles_count(), 2);

    bankmem_host_free();
}

/* --- undo ------------------------------------------------------------ */

static void test_undo(void)
{
    cell_record_t rec;
    char buf[WB_TEXT_MAX];

    setup();
    CHECK_EQ(wb_can_undo(), 0);
    CHECK_EQ(wb_dirty, 0);

    /* Overwrite, then take it back. */
    wb_set_text(0, 0, "first");
    CHECK_EQ(wb_dirty, 1);
    wb_set_text(0, 0, "second");
    disp(0, 0, buf); CHECK_STR(buf, "second");

    CHECK_EQ(wb_can_undo(), 1);
    CHECK_EQ(wb_undo(), ERR_OK);
    disp(0, 0, buf); CHECK_STR(buf, "first");

    /* One level only: a second undo does nothing rather than stepping back
     * further or corrupting anything. */
    CHECK_EQ(wb_can_undo(), 0);
    CHECK_EQ(wb_undo(), ERR_OK);
    disp(0, 0, buf); CHECK_STR(buf, "first");

    /* Undoing the creation of a cell removes it. */
    wb_set_text(5, 5, "new");
    CHECK_EQ(wb_get(5, 5, &rec), 1);
    wb_undo();
    CHECK_EQ(wb_get(5, 5, &rec), 0);

    /* Undoing a delete brings the value back. */
    wb_set_text(6, 6, "keep");
    wb_clear(6, 6);
    CHECK_EQ(wb_get(6, 6, &rec), 0);
    wb_undo();
    CHECK_EQ(wb_get(6, 6, &rec), 1);
    disp(6, 6, buf); CHECK_STR(buf, "keep");

    bankmem_host_free();
}

/* Editing text repeatedly must not leak string ids: the undo slot holds
 * exactly one snapshot, and everything it displaces has to be released. */
static void test_no_string_leak(void)
{
    uint16_t i;

    setup();
    for (i = 0; i < 200; ++i) {
        char t[8];
        t[0] = 'v'; t[1] = (char)('0' + (i % 10)); t[2] = '\0';
        wb_set_text(0, 0, t);
    }
    /* The live value plus at most the one snapshot undo is holding. */
    CHECK(strpool_count() <= 2);

    wb_clear(0, 0);
    wb_undo();                      /* restores, releasing the current one */
    wb_clear(0, 0);
    CHECK(strpool_count() <= 2);

    bankmem_host_free();
}

static void test_modified_flag(void)
{
    setup();
    CHECK_EQ(wb_dirty, 0);
    wb_set_text(0, 0, "x");
    CHECK_EQ(wb_dirty, 1);
    (wb_dirty = 0);
    CHECK_EQ(wb_dirty, 0);
    wb_clear(0, 0);
    CHECK_EQ(wb_dirty, 1);
    bankmem_host_free();
}

/* The worked example from the spec, end to end through the model. */
static void test_small_sheet(void)
{
    char buf[WB_TEXT_MAX];

    setup();
    wb_set_text(0, 0, "Name");
    wb_set_text(0, 1, "Quantity");
    wb_set_text(0, 2, "Price");
    wb_set_text(1, 0, "Cable");
    wb_set_text(1, 1, "4");
    wb_set_text(1, 2, "8.50");
    wb_set_text(2, 0, "Adapter");
    wb_set_text(2, 1, "2");
    wb_set_text(2, 2, "15.00");

    CHECK_EQ(wb_cells()->cell_count, 9);
    CHECK_EQ(wb_cells()->max_row, 2);
    CHECK_EQ(wb_cells()->max_col, 2);

    disp(1, 0, buf); CHECK_STR(buf, "Cable");
    disp(1, 1, buf); CHECK_STR(buf, "4");
    disp(2, 2, buf); CHECK_STR(buf, "15");
    disp(3, 0, buf); CHECK_STR(buf, "");

    bankmem_host_free();
}

/* Editing a formula gives back what the old one owned.
 *
 * It used not to. A formula cell owns a record in banked RAM and, through
 * it, a reference to its source text, and cell_release() knew about
 * neither -- so every edit leaked both. Two hundred edits of one cell left
 * two hundred dead strings in the pool and about 9 KB of banked RAM gone,
 * and every one of those strings was then written into the saved file.
 *
 * Counted rather than eyeballed, because the symptom is slow growth and
 * nothing about it looks wrong until the heap runs out. */
static void test_formula_edits_do_not_leak(void)
{
    uint32_t free_before;
    uint16_t alive_before;
    char b[32];
    uint8_t i;

    CHECK(bankmem_host_init(64));
    CHECK_EQ(bank_heap_init(1, 0), ERR_OK);
    CHECK_EQ(wb_init(), ERR_OK);

    CHECK_EQ(wb_set_text(0, 0, "=1+1"), ERR_OK);
    free_before  = bank_bytes_free();
    alive_before = strpool_count();

    for (i = 0; i < 100; ++i) {
        sprintf(b, "=%d*3", i);
        CHECK_EQ(wb_set_text(0, 0, b), ERR_OK);
    }

    /* One live source, and at most one more held by the undo snapshot. */
    CHECK(strpool_count() <= alive_before + 1);

    /* And the heap is where it was, give or take the one snapshot. The
     * leak was 47 bytes an edit, so 100 edits moved this by 4700. */
    CHECK(free_before - bank_bytes_free() < 200);

    /* Clearing gives back the last of it too. */
    CHECK_EQ(wb_clear(0, 0), ERR_OK);
    CHECK(strpool_count() <= alive_before + 1);
}

void test_workbook(void)
{
    test_formula_edits_do_not_leak();
    test_strpool();
    test_strpool_refcount();
    test_type_detection();
    test_alignment();
    test_number_formats();
    test_style_sharing();
    test_undo();
    test_no_string_leak();
    test_modified_flag();
    test_small_sheet();
}
