#include "workbook.h"
#include "workbook_priv.h"
#include "native_file.h"
#include "strings.h"
#include "../formula/formula.h"
#include "../platform/overlay.h"
#include "../util/number.h"
#include "../util/date.h"
#include "../platform/banked_ram.h"
#include <string.h>

/* 1 if the sheet holds at least one formula, so an edit to an ordinary cell
 * can skip loading the formula overlay entirely on a sheet with none. */
X16S_GOLDEN_BEGIN
static uint16_t formula_count;

/* Recalculation: automatic, or when asked.
 *
 * Stored as "manual" rather than "auto" so that zero is the right default.
 * Golden RAM is not cleared by the startup code -- golden_init() zeroes it
 * -- and a flag that has to be set to 1 afterwards is a flag someone will
 * forget to set. See banked_ram.h.
 *
 * The point of the switch: without dependency tracking every edit
 * recalculates every formula in the workbook, which is seconds on a sheet
 * with a few hundred of them. Turning it off makes typing instant and
 * leaves the answers stale until asked, which is the trade Lotus and
 * VisiCalc both offered and for the same reason. wb_calc_due remembers
 * that something was skipped, so the status bar can say so. */
X16S_GOLDEN_END

X16S_ZP_BEGIN
uint8_t wb_manual;
uint8_t wb_calc_due;
X16S_ZP_END

uint8_t wb_has_formulas(void) { return formula_count != 0; }

/* The sheets.
 *
 * All of them live in banked RAM; only the active one is here. A
 * cellstore_t is eight bytes — a handle and three counters — and cells_get()
 * already takes a pointer to one rather than assuming a global, so keeping
 * the active sheet in the same `sheet` variable it has always been in means
 * the renderer, the evaluator and every wb_* function carry on unchanged.
 * Switching writes this entry back and reads the next one in.
 *
 * Sixteen slots of 64 bytes, in banked RAM:
 *
 *     0   sheet_state_t       29    cells and column widths together
 *    32   name                32
 *
 * The state is one struct rather than four variables so that storing and
 * loading it is a single banked transfer each; cc65 charges heavily for
 * bank_read/bank_write with a computed offset. */
/* Golden RAM, not bss -- see cfg/x16sheet.cfg. The active sheet's state is
 * the largest block left in resident bss and wb_init() writes all of it.
 *
 * wb_sheet_n is deliberately outside the block: it starts at 1, and an
 * initialised variable belongs to DATA rather than to any bss segment, so
 * moving it here would be at best a no-op and at worst -- if the pragma
 * ever did take -- a workbook that starts with no sheets. */
X16S_GOLDEN_BEGIN
sheet_state_t sh;                   /* the active sheet */
X16S_ZP_END
/* Zero page: every sheet switch, every save and every cross-sheet formula
 * goes through this handle. It is four bytes and cc65 charges for all of
 * them at each use -- and zero page is not part of the resident image, so
 * they come back too. Measured: 36. */
X16S_ZP_BEGIN
handle_t      sheets;               /* all of them, banked */
X16S_ZP_END
X16S_GOLDEN_BEGIN
X16S_GOLDEN_END

/* Zero page: one byte, read 31 times across the program and on every
 * repaint of the tab row. See X16S_ZP_BEGIN in x16sheet.h. */
X16S_ZP_BEGIN
uint8_t       wb_sheet_i;              /* which is active */
X16S_ZP_END

uint8_t       wb_sheet_n = 1;          /* how many exist */


#define sheet   sh.store
X16S_ZP_BEGIN
uint8_t wb_dirty;               /* see workbook_priv.h */
X16S_ZP_END

/* Single-level undo. The slot owns whatever string its snapshot refers to,
 * so replacing a snapshot must release the old one.
 *
 * The two flags live in zero page and the record does not: the flags are
 * tested on every set and clear, the record only when an undo happens. */
X16S_ZP_BEGIN
static uint8_t undo_valid, undo_had_cell;
X16S_ZP_END

X16S_GOLDEN_BEGIN
static struct {
    uint16_t      row, col;
    cell_record_t prev;
} undo;
X16S_GOLDEN_END

/* --- string ownership ---------------------------------------------- */

static uint16_t cell_str_id(const cell_record_t *rec)
{
    if (rec->type != CELL_TEXT)
        return STR_NONE;
    return (uint16_t)(rec->val[0] | ((uint16_t)rec->val[1] << 8));
}

/* Give back everything the cell owned.
 *
 * A TEXT cell owns one reference to its string; a FORMULA cell also owns
 * its record in banked RAM and, through the record's FR_SRC field, a
 * reference to the source text.
 *
 * EVERY path that destroys a cell comes through here, which is what makes
 * this the only place that has to know. undo_snapshot must NOT call it for
 * the cell being snapshotted: the snapshot inherits what the cell owned
 * rather than taking its own reference.
 *
 * The bank test rather than `fr != H_NULL` because a four-byte comparison
 * is dear on this CPU and bank 0 is never handed out. It also skips a
 * formula cell read from a .X16S, whose val[] is zeroed until the compiler
 * rebuilds it. */
static void cell_release(const cell_record_t *rec)
{
    uint16_t id;

    if (rec->type == CELL_FORMULA) {
        handle_t fr;

        /* val[2] IS the bank: a handle is little-endian and H_BANK is
         * (h >> 16). Testing the byte costs a load and a branch, where
         * assembling the handle first to test it costs the assembly. */
        if (!rec->val[2])
            return;
        memcpy(&fr, rec->val, sizeof fr);
        strpool_unref(bank_peek16(fr, FR_SRC));
        bank_free(fr);
        return;
    }

    id = cell_str_id(rec);
    if (id != STR_NONE)
        strpool_unref(id);
}

/* Column widths, as a list of the ones that differ from the default.
 *
 * A list rather than a 256-byte table: real sheets set a handful of widths
 * and leave the rest alone. Past CW_MAX the extras keep the default, which
 * is what happens to a column the file never mentions anyway.
 */
/* CW_MAX is in workbook_priv.h: the widths travel with the sheet. */
static const char S_sheet1[] = "Sheet1";

uint8_t wb_col_width(uint16_t col)
{
    uint8_t i;

    for (i = 0; i < sh.cw_n; ++i)
        if (sh.cw_col[i] == (uint8_t)col && col < 256)
            return sh.cw_val[i];
    return X16S_DEF_COL_W;
}

void wb_set_col_width(uint16_t col, uint8_t w)
{
    uint8_t i;

    if (col > 255)
        return;
    if (w < X16S_MIN_COL_W)
        w = X16S_MIN_COL_W;
    else if (w > X16S_MAX_COL_W)
        w = X16S_MAX_COL_W;

    for (i = 0; i < sh.cw_n; ++i) {
        if (sh.cw_col[i] == (uint8_t)col) {
            sh.cw_val[i] = w;
            wb_dirty = 1;
            return;
        }
    }
    /* A column set to the default is not stored: that is what an absent
     * entry already means, and storing it would fill the list with rows
     * that say nothing -- which matters because the .X16S reader hands
     * this all 256 columns, nearly all of them default. */
    if (w != X16S_DEF_COL_W && sh.cw_n < CW_MAX) {
        sh.cw_col[sh.cw_n] = (uint8_t)col;
        sh.cw_val[sh.cw_n++] = w;
        wb_dirty = 1;
    }
}

/* --- sheets ----------------------------------------------------------- */

uint16_t slot_off(uint8_t n)
{
    return (uint16_t)(n * SH_SLOT);
}

/* Copy the active sheet's state out to its slot, and back again. The column
 * widths travel with it: they are per-sheet, and the resident cw_* arrays
 * are the active sheet's copy exactly as `sheet` is. */
void sheet_store(void)
{
    if (sheets != H_NULL)
        bank_write(sheets, slot_off(wb_sheet_i), &sh, sizeof sh);
}

void sheet_load(uint8_t n)
{
    wb_sheet_i = n;
    if (sheets != H_NULL)
        bank_read(sheets, slot_off(n), &sh, sizeof sh);
}


void wb_tab_name(uint8_t n, char *out)
{
    out[0] = '\0';
    if (n < wb_sheet_n && sheets != H_NULL) {
        bank_read(sheets, (uint16_t)(slot_off(n) + SH_NAME), out,
                  WB_TAB_NAME);
        out[WB_TAB_NAME - 1] = '\0';
    }
}


err_t wb_sheet_switch(uint8_t n)
{
    if (n >= wb_sheet_n)
        return ERR_NOTFOUND;
    if (n == wb_sheet_i)
        return ERR_OK;
    sheet_store();
    sheet_load(n);
    return ERR_OK;
}



err_t wb_init(void)
{
    err_t e;

    e = strpool_init();
    if (e != ERR_OK) return e;
    e = styles_init();
    if (e != ERR_OK) return e;
    e = cells_init(&sheet);
    if (e != ERR_OK) return e;

    /* One sheet, named the way a new workbook's first sheet is named. The
     * table is allocated here rather than once at start-up because
     * wb_reset() re-initialises the whole bank heap, and a handle taken
     * before that would address memory the next import has been given. */
    sh.cw_n = 0;
    wb_sheet_n = 1;
    wb_sheet_i = 0;
    sheets = bank_calloc(X16S_MAX_SHEETS * SH_SLOT);
    if (sheets == H_NULL)
        return ERR_NOMEM;
    sheet_store();
    bank_write(sheets, SH_NAME, S_sheet1, sizeof S_sheet1);
    /* And the two flags, which are no longer inside the struct -- moving
     * them to zero page took them out of this memset's reach, and a stale
     * "there is something to undo" survived a reset until a test said so. */
    memset(&undo, 0, sizeof undo);
    undo_valid = 0;
    undo_had_cell = 0;
    wb_dirty = 0;
    formula_count = 0;
    return ERR_OK;
}

err_t wb_reset(void)
{
    err_t e = bank_heap_init(1, 0);

    if (e != ERR_OK)
        return e;
    return wb_init();
}

uint8_t wb_get(uint16_t row, uint16_t col, cell_record_t *out)
{
    return cells_get(&sheet, row, col, out);
}

const cellstore_t *wb_cells(void) { return &sheet; }


/* --- undo ----------------------------------------------------------- */

/* Record the state of (row,col) before it changes. The previous snapshot is
 * dropped, releasing its string: only one edit is ever recoverable. */
static void undo_snapshot(uint16_t row, uint16_t col)
{
    if (undo_valid && undo_had_cell)
        cell_release(&undo.prev);

    undo.row = row;
    undo.col = col;
    undo_had_cell = cells_get(&sheet, row, col, &undo.prev);
    undo_valid = 1;
    /* No strpool_ref here: the snapshot inherits the reference the cell held,
     * because the caller is about to overwrite or delete that cell. */
}

#ifndef __CC65__
/* Host only: nothing in the program calls it, only the tests, and on
 * the machine every resident byte is contested. */
uint8_t wb_can_undo(void) { return undo_valid; }
#endif

err_t wb_undo(void)
{
    cell_record_t cur;
    uint8_t has_cur;

    if (!undo_valid)
        return ERR_OK;

    has_cur = cells_get(&sheet, undo.row, undo.col, &cur);
    if (has_cur)
        cell_release(&cur);          /* this value is being thrown away */

    if (undo_had_cell) {
        err_t e = cells_set(&sheet, undo.row, undo.col, &undo.prev);
        if (e != ERR_OK)
            return e;
    } else if (has_cur) {
        cells_delete(&sheet, undo.row, undo.col);
    }

    undo_valid = 0;                  /* the snapshot's reference moved back */
    wb_dirty = 1;
    return ERR_OK;
}

/* --- editing -------------------------------------------------------- */

static int str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a >= 'a' && *a <= 'z' ? (char)(*a - 32) : *a;
        char cb = *b >= 'a' && *b <= 'z' ? (char)(*b - 32) : *b;
        if (ca != cb)
            return 0;
        ++a; ++b;
    }
    return *a == *b;
}

err_t wb_set(uint16_t row, uint16_t col, const cell_record_t *rec)
{
    err_t e;
    cell_record_t old;
    uint8_t had = cells_get(&sheet, row, col, &old);

    undo_snapshot(row, col);
    e = cells_set(&sheet, row, col, rec);
    if (e == ERR_OK) {
        if (had && old.type == CELL_FORMULA && formula_count)
            --formula_count;
        if (rec->type == CELL_FORMULA)
            ++formula_count;
        wb_dirty = 1;
    }
    return e;
}

err_t wb_set_text(uint16_t row, uint16_t col, const char *text)
{
    cell_record_t rec;
    snum_t v;
    const char *p = text;

    while (*p == ' ')
        ++p;
    if (*p == '\0')
        return wb_clear(row, col);

    memset(&rec, 0, sizeof rec);
    rec.style = 0;

    if (str_ieq(text, "TRUE") || str_ieq(text, "FALSE")) {
        rec.type = CELL_BOOLEAN;
        rec.val[0] = str_ieq(text, "TRUE") ? 1 : 0;
    } else if (text[0] == '=') {
        /* Compiling needs OVL_FORMULA. If the overlay will not load there is
         * no safe fallback — storing the text as a label would silently turn
         * a formula into a string — so the edit is refused. */
        err_t e = ovl_require(OVL_FCOMPILE);
        if (e != ERR_OK)
            return e;
        e = formula_set(row, col, text);
        if (e == ERR_OK) {
            wb_dirty = 1;
            /* Compiling and evaluating are separate overlays, so this is a
             * second load — once per formula entered, not per keystroke. */
            wb_after_change();
        }
        return e;
    } else if (snum_parse(text, &v) == ERR_OK) {
        rec.type = CELL_NUMBER;
        memcpy(rec.val, v.b, 5);
    } else {
        uint16_t id, len = 0;
        err_t e;

        while (text[len] && len < X16S_MAX_TEXT_LEN)
            ++len;
        e = strpool_add(text, len, &id);
        if (e != ERR_OK)
            return e;

        rec.type = CELL_TEXT;
        rec.val[0] = (uint8_t)id;
        rec.val[1] = (uint8_t)(id >> 8);
    }

    {
        err_t e = wb_set(row, col, &rec);
        if (e == ERR_OK)
            wb_after_change();
        return e;
    }
}

/* Recalculate, if there is anything to recalculate. Without dependency
 * tracking every formula is potentially affected by every edit, so this is
 * called after any change to an ordinary cell. */
/* Open a .X16S: read it, then compile the formulas it was carrying.
 *
 * THE ONLY ENTRY POINT CALLERS SHOULD USE. x16s_load() is half the job --
 * it runs in OVL_FILEIO and can only collect formulas as pool ids, because
 * the compiler is in OVL_FCOMPILE. Skipping the second half does not fail
 * visibly: the cells all arrive and the formulas among them read zero. */
err_t x16s_open(const char *name)
{
    err_t e;

    if (ovl_require(OVL_FILEIO) != ERR_OK)
        return ERR_IO;
    e = x16s_load(name);
    if (e == ERR_OK)
        formula_compile_pending(0);
    return e;
}

void wb_after_change(void)
{
    if (!wb_has_formulas())
        return;
    if (wb_manual) {
        wb_calc_due = 1;        /* the answers on screen are now stale */
        return;
    }
    wb_calc_due = 0;
    if (ovl_require(OVL_FEVAL) != ERR_OK)
        return;         /* values keep their previous results */
    formula_recalc();
}

err_t wb_clear(uint16_t row, uint16_t col)
{
    cell_record_t cur;

    if (!cells_get(&sheet, row, col, &cur))
        return ERR_OK;

    if (cur.type == CELL_FORMULA && formula_count)
        --formula_count;
    undo_snapshot(row, col);         /* takes over the string reference */
    cells_delete(&sheet, row, col);
    wb_dirty = 1;
    wb_after_change();
    return ERR_OK;
}

/* --- rendering ------------------------------------------------------ */

uint8_t wb_bold(const cell_record_t *rec)
{
    cell_style_t st;

    styles_get(rec->style, &st);
    return (uint8_t)(st.flags & STY_BOLD);
}

uint8_t wb_align_right(const cell_record_t *rec)
{
    cell_style_t st;
    uint8_t a;

    styles_get(rec->style, &st);
    a = st.flags & STY_ALIGN_MASK;
    if (a == STY_ALIGN_LEFT)
        return 0;
    if (a == STY_ALIGN_RIGHT)
        return 1;
    /* Auto: numbers right, everything else left — the spreadsheet default
     * that makes a column of figures line up on the decimal point. A formula
     * aligns by what it produced, not by what it is. */
    if (rec->type == CELL_FORMULA) {
        handle_t fr = (handle_t)rec->val[0]
                    | ((handle_t)rec->val[1] << 8)
                    | ((handle_t)rec->val[2] << 16)
                    | ((handle_t)rec->val[3] << 24);
        uint8_t k = bank_peek(fr, FR_KIND);
        return k == CELL_NUMBER || k == CELL_DATE;
    }
    return rec->type == CELL_NUMBER || rec->type == CELL_DATE;
}

/* Render a numeric value according to a style. */
static uint8_t format_number(const snum_t *v, const cell_style_t *st,
                             char *out, uint8_t max)
{
    snum_t t;
    uint8_t n;

    switch (st->number_format) {
    case NF_INTEGER:
        return snum_to_fixed(v, 0, out);

    case NF_DECIMAL:
        return snum_to_fixed(v, st->decimal_places, out);

    case NF_CURRENCY:
        out[0] = '$';
        n = snum_to_fixed(v, st->decimal_places, out + 1);
        return n ? (uint8_t)(n + 1) : 0;

    case NF_PERCENT:
        t = *v;
        {
            snum_t hundred;
            snum_from_i16(&hundred, 100);
            if (snum_mul(&t, &hundred) != ERR_OK)
                return 0;
        }
        n = snum_to_fixed(&t, st->decimal_places, out);
        if (!n)
            return 0;
        out[n] = '%';
        out[n + 1] = '\0';
        return (uint8_t)(n + 1);

    case NF_DATE: {
        /* A date is a number; the format is what makes it one. The whole
         * part is the day, the fraction is the time of day. */
        int32_t whole;

        if (snum_to_i32(v, &whole) != ERR_OK || whole < 0)
            break;                      /* not a date after all */

        if (max < 11)
            break;
        return date_format((uint32_t)whole, out);
    }

    /* A time shows its number for now. The conversion exists and is tested;
     * wiring it in costs resident bytes this build has not got, and a
     * visible serial is better than a wrong clock. */
    case NF_TIME:
    case NF_GENERAL:
    case NF_TEXT:
    default:
        (void)max;
        return snum_to_text(v, out);
    }

    /* NF_DATE broke out: the value is not a usable date, or there is no
     * room to write one. Show the number, which is what every other
     * unformattable case does. Falling off the end here returned whatever
     * was in the accumulator as a length, to a caller that renders it. */
    return snum_to_text(v, out);
}

static uint8_t render(uint16_t row, uint16_t col, char *out, uint8_t max,
                      uint8_t formatted)
{
    cell_record_t rec;
    cell_style_t  st;
    snum_t v;

    out[0] = '\0';
    if (!cells_get(&sheet, row, col, &rec))
        return 0;

    switch (rec.type) {
    case CELL_TEXT:
        return (uint8_t)strpool_get(cell_str_id(&rec), out, max);

    case CELL_BOOLEAN:
        strcpy(out, rec.val[0] ? "TRUE" : "FALSE");
        return rec.val[0] ? 4 : 5;

    case CELL_ERROR:
        strcpy(out, err_cell_text((err_t)rec.val[0]));
        return (uint8_t)strlen(out);

    case CELL_NUMBER:
    case CELL_DATE:
        memcpy(v.b, rec.val, 5);
        if (!formatted)
            return snum_to_text(&v, out);
        styles_get(rec.style, &st);
        return format_number(&v, &st, out, max);

    case CELL_FORMULA: {
        handle_t fr = (handle_t)rec.val[0]
                    | ((handle_t)rec.val[1] << 8)
                    | ((handle_t)rec.val[2] << 16)
                    | ((handle_t)rec.val[3] << 24);
        uint8_t kind;

        /* Editing shows the source; the grid shows the answer. */
        if (!formatted)
            return (uint8_t)strpool_get(bank_peek16(fr, FR_SRC), out, max);

        kind = bank_peek(fr, FR_KIND);
        if (kind == CELL_ERROR) {
            strcpy(out, err_cell_text((err_t)bank_peek(fr, FR_VALUE)));
            return (uint8_t)strlen(out);
        }
        if (kind == CELL_TEXT)
            return (uint8_t)strpool_get(bank_peek16(fr, FR_VALUE), out, max);
        bank_read(fr, FR_VALUE, v.b, 5);
        if (kind == CELL_BOOLEAN) {
            uint8_t t = !snum_is_zero((&v));
            strcpy(out, t ? "TRUE" : "FALSE");
            return t ? 4 : 5;
        }
        styles_get(rec.style, &st);
        return format_number(&v, &st, out, max);
    }

    default:
        return 0;
    }
}

uint8_t wb_display_text(uint16_t row, uint16_t col, char *out, uint8_t max)
{
    return render(row, col, out, max, 1);
}

uint8_t wb_edit_text(uint16_t row, uint16_t col, char *out, uint8_t max)
{
    return render(row, col, out, max, 0);
}
