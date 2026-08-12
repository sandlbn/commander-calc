#include "test.h"
#include "../src/platform/bankmem.h"
#include "../src/platform/banked_ram.h"
#include "../src/workbook/cells.h"
/* The row walkers are not resident and not declared in cells.h; each
 * overlay that needs them includes them. See cells_priv.h. */
#include "../src/workbook/cells_priv.h"
#include "../src/workbook/cells_iter.c"

static cellstore_t cs;

static void setup(void)
{
    CHECK(bankmem_host_init(64));
    CHECK_EQ(bank_heap_init(1, 0), ERR_OK);
    CHECK_EQ(cells_init(&cs), ERR_OK);
}

/* A cell holding a recognisable number, so misplacement is visible. */
static cell_record_t mk(uint16_t col, uint8_t tag)
{
    cell_record_t r;
    memset(&r, 0, sizeof r);
    r.col = (uint8_t)col;
    r.type = CELL_NUMBER;
    r.val[0] = tag;
    return r;
}

static void test_empty(void)
{
    cell_record_t got;

    setup();
    CHECK_EQ(cs.cell_count, 0);
    CHECK_EQ(cells_get(&cs, 0, 0, &got), 0);
    CHECK_EQ(cells_row_count(&cs, 0), 0);
    /* An empty sheet must report no next row rather than row 0. */
    CHECK_EQ(cells_next_row(&cs, 0), X16S_MAX_ROWS);
    bankmem_host_free();
}

static void test_set_and_get(void)
{
    cell_record_t r, got;

    setup();
    r = mk(0, 0xAA);
    CHECK_EQ(cells_set(&cs, 0, 0, &r), ERR_OK);
    CHECK_EQ(cs.cell_count, 1);

    CHECK_EQ(cells_get(&cs, 0, 0, &got), 1);
    CHECK_EQ(got.val[0], 0xAA);
    CHECK_EQ(got.type, CELL_NUMBER);
    CHECK_EQ(got.col, 0);

    /* Neighbours must stay empty — an off-by-one in the index would show
     * up here and nowhere else. */
    CHECK_EQ(cells_get(&cs, 0, 1, &got), 0);
    CHECK_EQ(cells_get(&cs, 1, 0, &got), 0);

    /* Overwriting is not a new cell. */
    r = mk(0, 0xBB);
    CHECK_EQ(cells_set(&cs, 0, 0, &r), ERR_OK);
    CHECK_EQ(cs.cell_count, 1);
    CHECK_EQ(cells_get(&cs, 0, 0, &got), 1);
    CHECK_EQ(got.val[0], 0xBB);

    bankmem_host_free();
}

/* Columns must come back sorted no matter what order they went in — the
 * renderer and the file writer both walk a row in order. */
static void test_sorted_insert(void)
{
    static const uint8_t order[] = { 5, 1, 9, 0, 3, 7, 2 };
    cell_record_t r, got;
    uint8_t i;
    uint16_t n;

    setup();
    for (i = 0; i < sizeof order; ++i) {
        r = mk(order[i], order[i]);
        CHECK_EQ(cells_set(&cs, 10, order[i], &r), ERR_OK);
    }
    CHECK_EQ(cs.cell_count, sizeof order);
    CHECK_EQ(cells_row_count(&cs, 10), sizeof order);

    for (n = 0; n + 1 < cells_row_count(&cs, 10); ++n) {
        cell_record_t a, b;
        CHECK_EQ(cells_row_at(&cs, 10, n, &a), 1);
        CHECK_EQ(cells_row_at(&cs, 10, (uint16_t)(n + 1), &b), 1);
        CHECK(a.col < b.col);
    }

    /* Every one still individually findable, with its own payload. */
    for (i = 0; i < sizeof order; ++i) {
        CHECK_EQ(cells_get(&cs, 10, order[i], &got), 1);
        CHECK_EQ(got.val[0], order[i]);
    }
    bankmem_host_free();
}

/* Growth past the initial capacity must not lose or reorder anything. */
static void test_row_growth(void)
{
    cell_record_t r, got;
    uint16_t c;

    setup();
    for (c = 0; c < X16S_MAX_COLS; ++c) {
        r = mk(c, (uint8_t)(c ^ 0x5A));
        CHECK_EQ(cells_set(&cs, 3, c, &r), ERR_OK);
    }
    CHECK_EQ(cells_row_count(&cs, 3), X16S_MAX_COLS);
    for (c = 0; c < X16S_MAX_COLS; ++c) {
        CHECK_EQ(cells_get(&cs, 3, c, &got), 1);
        CHECK_EQ(got.val[0], (uint8_t)(c ^ 0x5A));
        CHECK_EQ(got.col, (uint8_t)c);
    }
    bankmem_host_free();
}

static void test_delete(void)
{
    cell_record_t r, got;
    uint8_t i;

    setup();
    for (i = 0; i < 5; ++i) {
        r = mk(i, (uint8_t)(i + 1));
        cells_set(&cs, 7, i, &r);
    }

    CHECK_EQ(cells_delete(&cs, 7, 2), 1);
    CHECK_EQ(cs.cell_count, 4);
    CHECK_EQ(cells_get(&cs, 7, 2, &got), 0);

    /* The survivors must close ranks in order. */
    CHECK_EQ(cells_row_count(&cs, 7), 4);
    CHECK_EQ(cells_row_at(&cs, 7, 0, &got), 1); CHECK_EQ(got.col, 0);
    CHECK_EQ(cells_row_at(&cs, 7, 1, &got), 1); CHECK_EQ(got.col, 1);
    CHECK_EQ(cells_row_at(&cs, 7, 2, &got), 1); CHECK_EQ(got.col, 3);
    CHECK_EQ(cells_row_at(&cs, 7, 3, &got), 1); CHECK_EQ(got.col, 4);

    /* Deleting what is not there changes nothing. */
    CHECK_EQ(cells_delete(&cs, 7, 2), 0);
    CHECK_EQ(cells_delete(&cs, 99, 0), 0);
    CHECK_EQ(cs.cell_count, 4);

    /* Emptying a row is fine, and the row can be used again afterwards. */
    for (i = 0; i < 5; ++i)
        cells_delete(&cs, 7, i);
    CHECK_EQ(cs.cell_count, 0);
    CHECK_EQ(cells_row_count(&cs, 7), 0);
    r = mk(1, 0x42);
    CHECK_EQ(cells_set(&cs, 7, 1, &r), ERR_OK);
    CHECK_EQ(cells_get(&cs, 7, 1, &got), 1);
    CHECK_EQ(got.val[0], 0x42);

    bankmem_host_free();
}

/* The whole point of the structure: A1, B1 and A10000 cost three records. */
static void test_sparseness(void)
{
    cell_record_t r, got;
    uint32_t before, after;

    setup();
    before = bank_bytes_free();

    r = mk(0, 1); cells_set(&cs, 0, 0, &r);
    r = mk(1, 2); cells_set(&cs, 0, 1, &r);
    r = mk(0, 3); cells_set(&cs, 9999, 0, &r);

    CHECK_EQ(cs.cell_count, 3);
    CHECK_EQ(cells_get(&cs, 9999, 0, &got), 1);
    CHECK_EQ(got.val[0], 3);
    CHECK_EQ(cells_get(&cs, 5000, 0, &got), 0);

    /* Two bands touched: the directory plus two bands plus two cell arrays,
     * nowhere near ten thousand rows of storage. */
    after = bank_bytes_free();
    CHECK(before - after < 8192);

    bankmem_host_free();
}

static void test_used_range(void)
{
    cell_record_t r;

    setup();
    r = mk(3, 1); cells_set(&cs, 20, 3, &r);
    CHECK_EQ(cs.max_row, 20);
    CHECK_EQ(cs.max_col, 3);

    r = mk(1, 1); cells_set(&cs, 40, 1, &r);
    CHECK_EQ(cs.max_row, 40);
    CHECK_EQ(cs.max_col, 3);              /* a wider column already seen */

    r = mk(9, 1); cells_set(&cs, 5, 9, &r);
    CHECK_EQ(cs.max_row, 40);
    CHECK_EQ(cs.max_col, 9);

    bankmem_host_free();
}

static void test_next_row(void)
{
    cell_record_t r;

    setup();
    r = mk(0, 1); cells_set(&cs, 2, 0, &r);
    r = mk(0, 1); cells_set(&cs, 900, 0, &r);   /* a different band */

    CHECK_EQ(cells_next_row(&cs, 0), 2);
    CHECK_EQ(cells_next_row(&cs, 2), 2);
    CHECK_EQ(cells_next_row(&cs, 3), 900);
    CHECK_EQ(cells_next_row(&cs, 901), X16S_MAX_ROWS);

    bankmem_host_free();
}

static void test_bounds(void)
{
    cell_record_t r = mk(0, 1);

    setup();
    /* The last legal cell must work... */
    CHECK_EQ(cells_set(&cs, X16S_MAX_ROWS - 1, X16S_MAX_COLS - 1, &r), ERR_OK);
    /* ...and one past it must be refused, not silently wrapped. */
    CHECK_EQ(cells_set(&cs, X16S_MAX_ROWS, 0, &r), ERR_REF);
    CHECK_EQ(cells_set(&cs, 0, X16S_MAX_COLS, &r), ERR_REF);
    bankmem_host_free();
}

void test_cells(void)
{
    test_empty();
    test_set_and_get();
    test_sorted_insert();
    test_row_growth();
    test_delete();
    test_sparseness();
    test_used_range();
    test_next_row();
    test_bounds();
}
