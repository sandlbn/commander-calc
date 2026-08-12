/* fref.c -- rewriting the cell references inside formulas.
 *
 * Every operation that moves a cell has to move what its formulas point
 * at: inserting and deleting rows, sorting, undoing a sort, pasting. All
 * of it is done on the SOURCE TEXT and none of it compiles.
 *
 * WHY IT IS NOT IN OVL_FCOMPILE, where it started. It called formula_set()
 * to put each rewritten formula back, so it had to live beside the
 * compiler -- and compile.c filled that overlay. Teaching the rewriter
 * about `$` needed 84 bytes more than existed, and about column letters 95.
 *
 * Text, and not the bytecode, and that is forced. The compiler parses `$`
 * and throws it away, so the bytecode cannot tell an absolute reference
 * from a relative one; OP_PUSH_CELL is a full u16 row and u8 col with no
 * spare bit to record it in, and a new opcode would mean changing the
 * evaluator, which has twenty-five bytes free. The source text is the only
 * place the `$` survives.
 *
 * So the rewriting happens here and the compiling happens later:
 * shift_one() puts the new source in the string pool and names the cell in
 * formula_pending, and resident code calls formula_compile_pending(), which
 * loads OVL_FCOMPILE once for the batch. That queue exists for exactly this
 * -- see formula_pending.c, written for two importers with the same
 * problem.
 *
 * The queue holds FPEND_MAX entries, so a sheet with more formulas than
 * that is done in passes: these functions return 1 for "full, drain me and
 * call again", and the caller loops. Nothing is capped.
 */
#include "formula.h"
#include "../x16sheet.h"
#include "../workbook/workbook.h"
#include "../workbook/workbook_priv.h"
#include "../workbook/cells.h"
#include "../workbook/strings.h"
#include "../platform/banked_ram.h"
#include "../ui/menu.h"     /* sort_undo_*, clip_*, col_shift_* */
#include "../ui/grid.h"
#include "../ui/screen.h"
#include "../platform/keyboard.h"
#include <string.h>

/* The scanner's own, as in compile.c -- two macros are cheaper to repeat
 * than a header to share them. */
#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define IS_ALPHA(c) (((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z'))
#define UP(c)       ((char)(((c) >= 'a' && (c) <= 'z') ? (c) - 32 : (c)))

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY17")
#  pragma rodata-name (push, "OVL17RO")
#  pragma bss-name (push, "OVL17BSS")
#endif

/* --- adjusting references when rows move ------------------------------
 *
 * Insert a row above =SUM(B2:B9) and it must become B2:B10, or the formula
 * quietly means something else.
 *
 * Rewritten as SOURCE TEXT and recompiled, never as bytecode: the source is
 * what the formula bar shows and what a re-edit starts from, so changing
 * only the bytecode would leave the two contradicting each other.
 *
 * `$` is deliberately ignored here. An absolute reference is absolute
 * against copying; when a row is physically inserted, everything below the
 * cut moves whether anchored or not.
 *
 * TWO DELIBERATE LIMITS, each with a test: a reference carrying a sheet
 * name is left alone, and deleting the row a reference points at leaves it
 * pointing at that row number rather than at #REF!. See
 * docs/design/formulas.md.
 */
/* Not X16S_MAX_FORMULA_LEN (255) twice: two buffers that size overflowed
 * this overlay by 132 bytes. A formula longer than this is SKIPPED rather
 * than truncated -- see formula_shift_rows() -- because a half-rewritten
 * formula is worse than one that did not move. 128 characters is four
 * times the longest formula in any of the example workbooks. */
#define SHIFT_MAX 128
static char shift_in[SHIFT_MAX + 1];
static char shift_out[SHIFT_MAX + 5];   /* every row number may grow a digit */

/* Append `v` in decimal. Counted down by tens rather than divided: this
 * overlay has no other use for cc65's division helpers. */
static uint8_t put_row(char *out, uint8_t o, uint16_t v)
{
    char d[6];
    uint8_t n = 0;

    do {
        uint16_t t = v, q = 0;
        while (t >= 10) { t -= 10; ++q; }
        d[n++] = (char)('0' + t);
        v = q;
    } while (v);
    while (n)
        out[o++] = d[--n];
    return o;
}

/* Rewrite one formula. Returns 1 if anything moved. */
/* Column letters, both ways. A1 style runs A..Z then AA..IV: a bijective
 * base 26, which is why the decode adds one per digit and the encode takes
 * one back off before recursing. Counted down by twenty-sixes rather than
 * divided, for the reason put_row() is: no division helpers in here. */
static uint16_t col_of(const char *s, uint8_t n)
{
    uint16_t c = 0;

    while (n--) {
        /* The character first. UP() is a macro that names its argument
         * three times, so UP(*s++) walked the pointer up to three
         * characters per letter and decoded column A as 65520. */
        char ch = *s++;

        c = (uint16_t)(c * 26 + (uint16_t)(UP(ch) - 'A' + 1));
    }
    return (uint16_t)(c - 1);
}

static uint8_t put_col(char *out, uint8_t o, uint16_t col)
{
    char d[3];
    uint8_t n = 0;

    for (;;) {
        uint16_t t = col, q = 0;

        while (t >= 26) { t -= 26; ++q; }
        d[n++] = (char)('A' + t);
        if (q == 0)
            break;
        col = (uint16_t)(q - 1);
    }
    while (n)
        out[o++] = d[--n];
    return o;
}

/* How far the columns move, and the first column that moves.
 *
 * A paste moves every column by the block's offset, so shift_cat is 0.
 * Inserting a column moves only those at or past the cut, exactly as `at`
 * does for rows. Both are set explicitly at each entry point rather than
 * trusted to start that way -- an overlay's bss is never zeroed. */
static int8_t   shift_dcol;
static uint16_t shift_cat;

/* `at` is the first row affected OUTRIGHT -- the caller works that out,
 * because inserting and deleting disagree about the row on the cut and a
 * sort has no cut at all and passes zero.
 *
 * A reference that would move above row 1 is left where it is. It should
 * be #REF!, which is what a real spreadsheet says, and there is no way to
 * spell that which the compiler will take back. */
static uint8_t shift_text(uint16_t at, int8_t by)
{
    const char *in = shift_in;
    uint8_t i = 0, o = 0, changed = 0, skip = 0;
    uint8_t cabs = 0, rabs = 0, cstart = 0;

    while (in[i]) {
        uint8_t j = i, letters = 0, digits = 0;
        uint16_t row = 0;

        /* A '!' means the next reference -- and the far end of a range, if
         * it is one -- names another sheet. Not ours to touch. */
        if (in[i] == '!') {
            skip = 1;
            shift_out[o++] = in[i++];
            continue;
        }

        /* `$` is REMEMBERED now, not merely stepped over. The source text
         * still carries it even though the bytecode throws it away, and
         * that is the whole reason a text rewriter can honour an absolute
         * reference when the evaluator cannot. */
        cabs = (uint8_t)(in[j] == '$');
        if (cabs)
            ++j;
        cstart = j;
        while (IS_ALPHA(in[j])) {
            ++letters;
            ++j;
        }
        if (letters && letters <= 2) {
            uint8_t k = j;

            rabs = (uint8_t)(in[k] == '$');
            if (rabs)
                ++k;
            while (IS_DIGIT(in[k])) {
                row = (uint16_t)(row * 10 + (uint8_t)(in[k] - '0'));
                ++digits;
                ++k;
            }
            /* A1 is a reference; SUM( and A1B are not. */
            if (digits && digits <= 5 && !IS_ALPHA(in[k])) {
                if (skip) {
                    /* Carry the skip across the ':' of a range. */
                    skip = (in[k] == ':');
                } else {
                    uint16_t col = col_of(in + cstart, letters);
                    uint8_t mr = (uint8_t)(by && !rabs && row >= 1
                                           && (uint16_t)(row - 1) >= at
                                           && (int16_t)row + by >= 1);
                    uint8_t mc = (uint8_t)(!cabs && shift_dcol
                                    && col >= shift_cat
                                    && (int16_t)col + shift_dcol >= 0
                                    && (int16_t)col + shift_dcol
                                           < X16S_MAX_COLS);

                    if (mr || mc) {
                        if (cabs)
                            shift_out[o++] = '$';
                        o = put_col(shift_out, o,
                                    mc ? (uint16_t)(col + shift_dcol) : col);
                        if (rabs)
                            shift_out[o++] = '$';
                        o = put_row(shift_out, o,
                                    mr ? (uint16_t)(row + by) : row);
                        i = k;
                        changed = 1;
                        continue;
                    }
                }
                while (i < k)
                    shift_out[o++] = in[i++];
                continue;
            }
        }

        /* Not a reference: copy what we looked at and carry on. A sheet
         * name is longer than two letters and lands here, which is how the
         * '!' after it gets seen. */
        if (letters)
            while (i < j)
                shift_out[o++] = in[i++];
        else
            shift_out[o++] = in[i++];
    }
    shift_out[o] = '\0';
    return changed;
}

/* Read one formula, rewrite it and queue it. Does not compile: the compiler
 * is OVL_FCOMPILE and this is not.
 *
 * Returns 1 when the QUEUE IS FULL -- not "something changed" -- which is
 * the caller's signal to stop and let resident code drain it.
 */
/* Put a cell's new source in the pool and name the cell in the queue.
 * formula_compile_pending() turns that into a compiled cell. Modelled on
 * note_formula() in csv.c -- the same six-byte entry.
 *
 * 1 means the queue is now full: drain it and call again. */
static uint8_t queue_at(uint16_t r, uint16_t c, const char *text)
{
    uint8_t ent[FPEND_FIELD];
    uint16_t nid;

    if (formula_pending == H_NULL || formula_pending_n >= FPEND_MAX)
        return 1;
    if (strpool_add(text, (uint16_t)strlen(text), &nid) != ERR_OK)
        return 0;

    ent[0] = wb_sheet_i;
    ent[1] = (uint8_t)r;
    ent[2] = (uint8_t)(r >> 8);
    ent[3] = (uint8_t)c;
    ent[4] = (uint8_t)nid;
    ent[5] = (uint8_t)(nid >> 8);
    bank_write(formula_pending, (uint16_t)(formula_pending_n * FPEND_FIELD),
               ent, FPEND_FIELD);
    ++formula_pending_n;
    return (uint8_t)(formula_pending_n >= FPEND_MAX);
}

static uint8_t shift_one(uint16_t r, uint16_t c, uint16_t at, int8_t by)
{
    cell_record_t rec;
    handle_t fr;
    uint16_t sid;

    if (!cells_get(wb_cells(), r, c, &rec) || rec.type != CELL_FORMULA)
        return 0;
    memcpy(&fr, rec.val, sizeof fr);
    sid = bank_peek16(fr, FR_SRC);

    /* Too long to rewrite safely: leave it exactly as it is. It will point
     * at the wrong rows, which is what this exists to prevent -- but a
     * formula truncated and recompiled is destroyed. */
    if (strpool_len(sid) > SHIFT_MAX)
        return 0;
    strpool_get(sid, shift_in, sizeof shift_in);
    if (!shift_text(at, by))
        return 0;

    return queue_at(r, c, shift_out);
}

/* Where a pass stopped, so the next one carries on.
 *
 * OVL17BSS, and an overlay's bss is NEVER ZEROED -- see the note beside
 * OVL_CODE_BEGIN in x16sheet.h. So `restart` is an argument, not a static
 * that happens to be clear: the caller says when a run begins. */
static uint16_t res_r, res_c;

uint8_t formula_shift(uint16_t at, int8_t by, uint8_t cols, uint8_t restart)
{
    const cellstore_t *cs = wb_cells();

    if (cs->cell_count == 0)
        return 0;
    if (restart) {
        res_r = 0;
        res_c = 0;
    }

    /* Inserting moves the row or column ON the cut; deleting moves the one
     * after it, which is what makes a range shrink correctly at both ends. */
    if (by < 0)
        ++at;

    /* The only difference between the two axes: which pair shift_text()
     * reads. One function rather than two near-identical ones, because the
     * caller's second branch cost more than this flag does. */
    shift_dcol = cols ? by : 0;
    shift_cat = cols ? at : 0;
    if (cols) {
        at = 0;
        by = 0;
    }

    for (; res_r <= cs->max_row; ++res_r, res_c = 0)
        for (; res_c <= cs->max_col; ++res_c)
            if (shift_one(res_r, res_c, at, by)) {
                /* PAST the cell that filled the queue, not at it: coming
                 * back to it would shift it a second time. */
                ++res_c;
                return 1;
            }
    shift_dcol = 0;
    return 0;
}

/* --- after a sort ------------------------------------------------------
 *
 * A sort is a permutation, not a shift, so the row-shifting path does not
 * apply. A relative reference is an offset from the cell holding it, so
 * when that cell moves n rows its references move with it -- the same thing
 * a spreadsheet does when a formula is dragged.
 *
 * The permutation says which original row landed at each position, so the
 * cell now at base+i came from base+map[i] and moved i - map[i] rows. Undo
 * is the same arithmetic with the sign reversed and MUST RUN BEFORE THE
 * ROWS MOVE, because sort_undo() frees the permutation this reads.
 *
 * A range among the sorted rows is shifted like any other reference, which
 * is right only if the rows it names moved together; this is why sorting
 * asks first. See docs/design/formulas.md.
 */
/* --- pasting formulas --------------------------------------------------
 *
 * The clipboard's non-formula cells are written by OVL_MENU, which cannot
 * compile: wb_set_text() on a `=` loads THIS overlay over the one making
 * the call. So the formulas are left behind and the grid comes here for
 * them, with the block still in banked RAM where OVL_MENU put it.
 *
 * Its own clip_at(), because menu.c's is in another overlay. Eight lines,
 * the same bargain the file layer makes when an overlay needs one.
 */
static uint16_t clip_at(uint16_t off, char *out, uint16_t max)
{
    uint16_t n = 0;

    do {
        out[n] = (char)bank_peek(clip_h, (uint16_t)(off + n));
    } while (out[n] && ++n < (uint16_t)(max - 1));
    out[n] = 0;
    return (uint16_t)(off + n + 1);
}

uint8_t formula_paste(uint16_t row, uint16_t col, uint8_t restart)
{
    uint16_t r, c, off = 0;

    if (restart) {
        res_r = 0;
        res_c = 0;
    }

    if (clip_rows == 0 || clip_h == H_NULL)
        return 0;

    /* Walks from the start every time and skips what it has already
     * queued. The clipboard is small and a second pass is rare; an index
     * into it would have to be kept in step with the offset walk, and that
     * is a bug waiting rather than a saving. */
    for (r = 0; r < clip_rows; ++r)
        for (c = 0; c < clip_cols; ++c) {
            uint16_t dr = (uint16_t)(row + r);
            uint16_t dc = (uint16_t)(col + c);

            off = clip_at(off, shift_in, sizeof shift_in);
            if (shift_in[0] != '=')
                continue;
            if (dr >= X16S_MAX_ROWS || dc >= X16S_MAX_COLS)
                continue;               /* clipped at the edge */
            if (r < res_r || (r == res_r && c < res_c))
                continue;               /* done on an earlier pass */

            /* This is what "relative" means: a reference with no `$` is an
             * offset from the cell holding it, so moving the cell moves
             * the reference with it. `at` is 0 -- every row, not a cut.
             *
             * NOT formula_set(): the compiler is OVL_FCOMPILE and this is
             * not it. That call is what kept all of this stuck there. */
            shift_dcol = (int8_t)((int16_t)col - (int16_t)clip_col0);
            shift_cat = 0;              /* every column, not from a cut */
            if (!shift_text(0, (int8_t)((int16_t)row - (int16_t)clip_row0)))
                strcpy(shift_out, shift_in);
            shift_dcol = 0;

            if (queue_at(dr, dc, shift_out)) {
                res_r = r;
                res_c = (uint16_t)(c + 1);
                return 1;
            }
        }
    return 0;
}

/* --- inserting and deleting a column -----------------------------------
 *
 * The mirror of row_cmd() in menu.c, and HERE rather than beside it
 * because OVL_MENU has two bytes free and this overlay has thousands --
 * and because the reference shifting a column move needs is already here.
 *
 * Records are moved, not copied: cells_set and cells_delete leave
 * reference counts alone, so a cell changes address and nothing else. The
 * same property the sort relies on, and the same reason nothing here can
 * leak a string or free one twice.
 *
 * Deleting asks first, because it destroys a column and there is no undo
 * for it. Inserting does not: the way back is to delete it again.
 */
static const char S_delcol[] = "Delete column? (y/n)";

static void col_move(uint16_t from, uint16_t to, uint16_t rows)
{
    cellstore_t *cs = (cellstore_t *)wb_cells();
    cell_record_t rec;
    uint16_t r;

    for (r = 0; r < rows; ++r) {
        if (cells_get(cs, r, from, &rec)) {
            cells_delete(cs, r, from);
            rec.col = (uint8_t)to;
            cells_set(cs, r, to, &rec);
        } else {
            cells_delete(cs, r, to);
        }
    }
}

uint8_t col_cmd(uint8_t del)
{
    const cellstore_t *cs = wb_cells();
    uint16_t at = grid_state()->cur_col;
    uint16_t last = cs->max_col, rows, c;

    if (cs->cell_count == 0 || at > last)
        return 0;
    rows = (uint16_t)(cs->max_row + 1);

    if (del) {
        uint8_t y = (uint8_t)(screen_rows() - 1);

        screen_fill(0, y, screen_cols(), ' ', COLOR(COL_BLACK, COL_YELLOW));
        screen_text(0, y, S_delcol, (uint8_t)(sizeof S_delcol - 1),
                    COLOR(COL_BLACK, COL_YELLOW));
        c = kbd_wait();
        if (c != 'y' && c != 'Y')
            return 0;

        for (c = at; c < last; ++c)
            col_move((uint16_t)(c + 1), c, rows);
        for (c = 0; c < rows; ++c)      /* the vacated last column */
            cells_delete((cellstore_t *)cs, c, last);
    } else {
        if (last >= X16S_MAX_COLS - 1)
            return 0;                   /* nowhere for the last column */
        for (c = (uint16_t)(last + 1); c > at; --c)
            col_move((uint16_t)(c - 1), c, rows);
        /* col_move emptied the cursor's column on its way past. */
    }

    wb_dirty = 1;
    return 1;
}

uint8_t formula_sorted(uint8_t undo, uint8_t restart)
{
    const cellstore_t *cs = wb_cells();
    uint16_t i, c;

    if (sort_undo_n == 0 || cs->cell_count == 0)
        return 0;
    shift_dcol = 0;                     /* a sort moves rows, not columns */
    shift_cat = 0;
    if (restart) {
        res_r = 0;
        res_c = 0;
    }

    for (i = res_r; i < sort_undo_n; ++i) {
        int16_t d = (int16_t)i
                  - (int16_t)bank_peek16(sort_undo_idx, (uint16_t)(i * 2));
        uint16_t row;

        if (undo)
            d = (int16_t)-d;
        if (d == 0 || d > 127 || d < -128)
            continue;               /* the shift is one byte; see above */

        row = (uint16_t)(sort_undo_base + i);
        for (c = res_c; c <= cs->max_col; ++c)
            if (shift_one(row, c, 0, (int8_t)d)) {
                res_r = i;
                res_c = (uint16_t)(c + 1);
                return 1;               /* queue full: drain and resume */
            }
        res_c = 0;
    }
    return 0;
}

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
