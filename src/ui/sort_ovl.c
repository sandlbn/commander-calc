/* sort_ovl.c — sorting the used range by the cursor's column.
 *
 * The half that does the work. sort_ask() in filedlg.c does the half that
 * talks to the user, and they are in different overlays because this came
 * to about 1800 bytes and the dialogs' overlay had 180 to spare. Resident
 * code runs one then the other; nothing here calls into another overlay.
 *
 * WHOLE ROWS MOVE, which is the only sort worth having: sorting one column
 * and leaving its neighbours where they were silently shreds the sheet.
 */
#include "../workbook/workbook.h"
#include "../workbook/workbook_priv.h"
#include "../workbook/cells.h"
#include "../workbook/strings.h"
#include "../formula/formula.h"
#include "../util/number.h"
#include "../platform/banked_ram.h"
#include "grid.h"
#include "menu.h"
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY5")
#  pragma rodata-name (push, "OVL5RO")
#  pragma bss-name (push, "OVL5BSS")
#endif

static char up(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

/* --- sorting ---------------------------------------------------------- *
 *
 * Sort the used range by the column the cursor is in. Here, in the dialog
 * overlay, because it has to ask a question first and the asking and the
 * doing must be in the same overlay -- the same reason the sheet commands
 * are here. The grid pays one case label for it.
 *
 * WHOLE ROWS MOVE, which is the only sort worth having: sorting one column
 * and leaving its neighbours behind silently shreds the sheet.
 *
 * Two things this deliberately does not do.
 *
 * It does not adjust references. A formula that says =A1 still says =A1
 * after its row moves, and a formula outside the range pointing into it is
 * now pointing at different data. That needs the same reference-rewriting
 * machinery paste wants, so the range is checked for formulas first and
 * the user is asked rather than surprised.
 *
 * It compares text on the first SORT_KEY_CHARS characters. A pool string
 * can be a kilobyte and two buffers of that would not fit in this overlay,
 * let alone on the C stack.
 */
#define SORT_KEY_CHARS 32

static char     sort_a[SORT_KEY_CHARS + 1];
static char     sort_b[SORT_KEY_CHARS + 1];
static handle_t sort_idx;               /* row order, being sorted */
static uint16_t sort_first;             /* the first row of the range */

/* The index is relative to the range; the sheet is not. */
#define sort_row(i) ((uint16_t)(sort_first + (i)))
static uint16_t sort_key;               /* the column to sort on */
static uint16_t sort_cols;              /* max_col + 1 */
static uint8_t  sort_desc;

#define IDX_AT(i)      bank_peek16(sort_idx, (uint16_t)((i) * 2))
#define IDX_PUT(i, v)  bank_poke16(sort_idx, (uint16_t)((i) * 2), (v))

/* Reduce a cell to something comparable: a rank, plus a number or text.
 *
 * Ranks order the kinds against each other -- numbers, then text, then
 * errors, then nothing at all -- which is what a spreadsheet does and what
 * keeps blank rows at the bottom instead of interleaved. */
static uint8_t key_of(uint16_t row, snum_t *n, char *buf)
{
    cell_record_t rec;
    uint8_t kind, val[5];
    handle_t fr;

    if (!cells_get(wb_cells(), sort_row(row), sort_key, &rec))
        return 3;

    kind = rec.type;
    memcpy(val, rec.val, 5);

    /* A formula sorts by what it worked out, not by its text. */
    if (kind == CELL_FORMULA) {
        memcpy(&fr, rec.val, sizeof fr);
        kind = bank_peek(fr, FR_KIND);
        bank_read(fr, FR_VALUE, val, 5);
    }

    if (kind == CELL_TEXT) {
        strpool_get((uint16_t)(val[0] | ((uint16_t)val[1] << 8)),
                    buf, SORT_KEY_CHARS + 1);

        /* Text that is a number sorts as a number.
         *
         * Reported from the machine: 1, 2, 3, 5, 456 came back as 1, 2, 3,
         * 456, 5. That is what comparing them as text does -- "456" is
         * less than "5" on the fourth character -- and a worksheet can
         * easily hold numbers as text, because an .xlsx may store them as
         * inline strings and nothing on the way in second-guesses that.
         *
         * Costs a parse per text cell of the key column, in an overlay
         * that has the room, and only while sorting. A cell that is not a
         * number fails to parse and stays text, which is the answer for
         * "Bracket". */
        if (snum_parse(buf, n) == ERR_OK)
            return 0;
        return 1;
    }
    if (kind == CELL_ERROR)
        return 2;

    memcpy(n->b, val, 5);
    return 0;
}

/* Negative when `ra` belongs above `rb`. Empty rows are pinned to the
 * bottom in both directions, which is why the descending flip happens
 * before the rank test rather than after it. */
static int8_t sort_cmp(uint16_t ra, uint16_t rb)
{
    snum_t na, nb;
    uint8_t ka, kb, i;
    int8_t r;

    ka = key_of(ra, &na, sort_a);
    kb = key_of(rb, &nb, sort_b);

    if (ka == 3 || kb == 3)
        return (ka == kb) ? 0 : (ka == 3 ? 1 : -1);

    if (ka != kb)
        r = (ka < kb) ? -1 : 1;
    else if (ka == 0)
        r = snum_cmp(&na, &nb);
    else if (ka == 1) {
        r = 0;
        for (i = 0; i <= SORT_KEY_CHARS; ++i) {
            char x = up(sort_a[i]), y = up(sort_b[i]);
            if (x != y) { r = (x < y) ? -1 : 1; break; }
            if (!x) break;
        }
    } else
        r = 0;

    return sort_desc ? (int8_t)-r : r;
}

/* Exchange two whole rows.
 *
 * cells_set and cells_delete leave reference counts alone -- cells.h says
 * so explicitly -- which is what makes this safe: a record changes address
 * without changing owner, so nothing here touches the string pool and
 * nothing can leak or be freed twice. */
static void row_swap(uint16_t a, uint16_t b)
{
    cellstore_t *cs = (cellstore_t *)wb_cells();
    cell_record_t ra, rb;
    uint16_t c;
    uint8_t ha, hb;

    for (c = 0; c < sort_cols; ++c) {
        ha = cells_get(cs, sort_row(a), c, &ra);
        hb = cells_get(cs, sort_row(b), c, &rb);

        if (hb) {
            rb.col = (uint8_t)c;
            cells_set(cs, sort_row(a), c, &rb);
        } else {
            cells_delete(cs, sort_row(a), c);
        }
        if (ha) {
            ra.col = (uint8_t)c;
            cells_set(cs, sort_row(b), c, &ra);
        } else {
            cells_delete(cs, sort_row(b), c);
        }
    }
}

/* Shell sort, over an index of row numbers held in banked RAM.
 *
 * Not a selection sort, which is the obvious thing and would be O(n^2):
 * the inventory sheet in DEMO.XLSX is twelve hundred rows, so that is over
 * a million comparisons of two banked cell reads apiece -- minutes. This
 * is nearer nine thousand. */
static void sort_index(uint16_t n)
{
    uint16_t gap, i, j, tmp;

    for (gap = n / 2; gap > 0; gap /= 2) {
        for (i = gap; i < n; ++i) {
            tmp = IDX_AT(i);
            for (j = i; j >= gap && sort_cmp(IDX_AT(j - gap), tmp) > 0;
                 j -= gap)
                IDX_PUT(j, IDX_AT(j - gap));
            IDX_PUT(j, tmp);
        }
    }
}

/* Realise the permutation in place, holding one cell rather than one row --
 * staging the sorted range in scratch memory would cost megabytes on a wide
 * sheet.
 *
 * IDX_AT(k) says which ORIGINAL row belongs at position k. NOTE THAT THE
 * OBVIOUS SWAP LOOP -- while (idx[i] != i) { swap rows; swap index } --
 * realises the INVERSE permutation, and comes out rotated rather than
 * sorted. Instead, for each position follow the chain forward until it
 * reaches a row not yet moved away, and swap with that. The index is only
 * ever read, never written.
 */
static void sort_permute(uint16_t n)
{
    uint16_t i, j;

    for (i = 0; i < n; ++i) {
        j = IDX_AT(i);
        /* Everything below i is already in place, so a source there has
         * itself been moved; follow where it went. */
        while (j < i)
            j = IDX_AT(j);
        if (j != i)
            row_swap(i, j);
    }
}

/* The working half. Everything the user could need telling about has
 * already been settled by sort_ask() in OVL_FILEDLG; this cannot fail in
 * any way worth reporting, which is what lets it live in an overlay with
 * no dialogs in it. */
void sort_run(handle_t idx, uint8_t desc)
{
    const cellstore_t *cs = wb_cells();
    uint16_t n, i;

    /* From the cursor's row down -- see sort_ask(). The index counts from
     * zero and sort_row() adds the base back on, so nothing else here has
     * to know where the range starts. */
    sort_first = sort_base();
    n = (uint16_t)(cs->max_row - sort_first + 1);

    sort_idx  = idx;
    sort_key  = grid_state()->cur_col;
    sort_cols = (uint16_t)(cs->max_col + 1);
    sort_desc = desc;

    for (i = 0; i < n; ++i)
        IDX_PUT(i, i);
    sort_index(n);
    sort_permute(n);

    /* Keep the permutation rather than freeing it: it is what undoing the
     * sort needs, and the previous one is no longer reachable. */
    bank_free(sort_undo_idx);
    sort_undo_idx   = sort_idx;
    sort_undo_n     = n;
    sort_undo_sheet = wb_sheet_i;
    sort_undo_base  = sort_first;
    sort_idx = H_NULL;

    /* NO wb_after_change() HERE. It is resident, but it loads OVL_FEVAL --
     * which is loaded over THIS overlay, and the return address is into
     * code that no longer exists. The recalculation is the caller's job,
     * once this has returned to resident code. */
    wb_touch();
}

/* Put the rows back where they were.
 *
 * The stored permutation says which original row ended up at each position,
 * so undoing it means applying the INVERSE. That is built into a second
 * block and handed to the same sort_permute() -- inverting a permutation in
 * place is fiddly and this is a few hundred bytes of banked RAM for the
 * duration of one operation.
 *
 * Refuses on a sheet that is not the one that was sorted, or one whose used
 * range has changed size since. Neither is a guess: applying a stale
 * permutation would move rows that were never sorted. */
void sort_undo(void)
{
    const cellstore_t *cs;
    handle_t inv;
    uint16_t n = sort_undo_n, i;

    if (!n || sort_undo_sheet != wb_sheet_i)
        return;

    cs = wb_cells();
    sort_first = sort_undo_base;
    if ((uint16_t)(cs->max_row - sort_first + 1) != n)
        return;

    inv = bank_alloc((uint16_t)(n * 2));
    if (inv == H_NULL)
        return;

    sort_idx = sort_undo_idx;
    for (i = 0; i < n; ++i)
        bank_poke16(inv, (uint16_t)(IDX_AT(i) * 2), i);

    sort_idx  = inv;
    sort_cols = (uint16_t)(cs->max_col + 1);
    sort_permute(n);

    bank_free(inv);
    bank_free(sort_undo_idx);
    sort_idx      = H_NULL;
    sort_undo_idx = H_NULL;
    /* Once only: applying the inverse twice is not a redo, it is a second
     * unrelated shuffle. */
    sort_undo_n = 0;

    wb_touch();          /* the recalculation is the caller's -- see above */
}

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
