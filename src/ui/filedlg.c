#include "filedlg.h"
#include "../import/xlsx_import.h"
#include "menu.h"
#include "../workbook/workbook.h"
#include "screen.h"
#include "editor.h"
#include "../platform/keyboard.h"
#include "../platform/mouse.h"
#include "../platform/banked_ram.h"
#include "../workbook/cells.h"
#include "../workbook/workbook_priv.h"
#include "../workbook/strings.h"
#include "../formula/formula.h"
#include "../util/number.h"
#include "grid.h"
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY1")
#  pragma rodata-name (push, "OVL1RO")
/* bss too. Whatever bss this file generates was landing in RESIDENT bss --
 * the scarcest memory in the program -- because this block pushed the other
 * two names and not this one, and it went unnoticed for a long time.
 * Anything here that has to outlive an overlay swap is in golden RAM
 * explicitly; everything else is scratch for one dialog, which is one
 * residency. */
#  pragma bss-name (push, "OVL1BSS")
#endif

/* Named arrays rather than literals: cc65 places a literal in RODATA
 * however rodata-name is set, but honours the pragma for a named const
 * array. These are read only while this overlay is loaded, so they
 * belong in its segment and not in resident memory. */
static const char S_failed_words[] = "Save\0Open\0Import\0Export";
static const char S_discard_words[] = "Open workbook\0New workbook\0Quit";
static const char S_discard[] = "discard unsaved changes?";
static const char S_failed[] = " failed";
static const char S_0[] = "press any key";
static const char S_1[] = "Y = yes, any other key = no";
static const char S_2[] = "Open";
static const char S_3[] = "no matching files on the card";
static const char S_4[] = "up/down select   RETURN open   ESC cancel";
static const char S_5[] = "Save workbook as";
static const char S_6[] = "RETURN accept   ESC cancel";
static const char S_7[] = "Name:";
static const char S_8[] = "end it .XLSX for Excel, .CSV for plain text";
static const char S_yes[] = "[  Yes  ]";
static const char S_no[]  = "[  No   ]";

#define K_RETURN 0x0D
#define K_ESC    0x1B
#define K_DOWN   0x11
#define K_UP     0x91

#define C_DLG   COLOR(COL_BLACK, COL_LGREY)
#define C_TITLE COLOR(COL_WHITE, COL_BLUE)
#define C_SEL   COLOR(COL_WHITE, COL_BLUE)
/* The name being typed sits in a field of its own colour. Without one the
 * only thing drawn in an empty dialog is the caret — a single blue cell in
 * the middle of a light grey box, which reads as a smudge rather than as a
 * place to type. */
#define C_FIELD COLOR(COL_BLACK, COL_WHITE)

#define DLG_X     12
#define DLG_W     52

/* The name field: after the "File name:" label, with room for the longest
 * name CMDR-DOS will accept. */
#define FIELD_X   ((uint8_t)(DLG_X + 13))
#define FIELD_W   ((uint8_t)(DLG_W - 17))
/* Names the Open and Import dialogs will show. The listing is drawn in one
 * go with no scrolling, so this is bounded by the screen: at 32 the box
 * ends around row 38 of 60. It costs LIST_MAX * FILE_NAME_MAX of banked
 * RAM and nothing resident.
 *
 * Must comfortably exceed the number of files sitting beside the program
 * -- the overlay images alone are seventeen -- or the list fills with .BIN
 * files and no workbook is ever shown. The extension filter below is the
 * other half of that. */
#define LIST_MAX  32

/* The listing lives in banked RAM, reached through a handle. It cannot go in
 * the overlay (an overlay's data is whatever the last load left behind) and
 * 384 bytes of resident bss for a buffer used only while a dialog is open is
 * a poor trade when the heap is right there.
 *
 * It is allocated and released within a single dialog. A handle kept in a
 * static across wb_reset() would point into a heap that no longer exists,
 * and Open is precisely the command that resets the heap. */
X16S_GOLDEN_BEGIN
static handle_t names_h;
static uint8_t  name_count;
X16S_GOLDEN_END

static void name_get(uint8_t i, char *out)
{
    if (names_h == H_NULL) {
        out[0] = '\0';
        return;
    }
    bank_read(names_h, (uint16_t)(i * FILE_NAME_MAX), out, FILE_NAME_MAX);
    out[FILE_NAME_MAX - 1] = '\0';
}

/* --- waiting, with the pointer ---------------------------------------- *
 *
 * Every dialog here was a kbd_wait() away from being unusable with a mouse:
 * it blocks, so nothing polls the pointer while a dialog is up, and the one
 * modal thing the user is being asked to answer was the one thing they
 * could not click. Rather than teach each dialog about the mouse, this
 * waits for either and reports a click AS A KEY, so all the existing key
 * handling below is unchanged.
 *
 * Where the click landed is left in dlg_cy and dlg_cx for the two dialogs
 * that care which row was hit. A click that means nothing in particular
 * comes back as K_RETURN, because "press any key" and "click anywhere" want
 * to be the same gesture.
 */
static uint8_t dlg_cy, dlg_cx;

/* Set by the dialog before waiting: the rows it will accept a click on. */
static uint8_t hot_y, hot_n;

static uint8_t dlg_wait(void)
{
    /* The press that opened the dialog may still be held -- a menu item is
     * chosen on a click and this can be the very next thing to run -- so
     * start as though it has been seen. */
    uint8_t was_down = 1;

    for (;;) {
        uint8_t k = kbd_get();
        uint8_t down;

        if (k) {
            dlg_cy = 0xFF;
            return k;
        }

#ifdef X16S_HOST
        /* Off the machine there is no pointer, and mouse_host.c never
         * reports one, so an empty key queue would spin here for ever.
         * Cancel, which is what an unscripted answer means in a test. */
        dlg_cy = 0xFF;
        return K_ESC;
#endif

        mouse_poll();
        down = (uint8_t)(mouse_btn & MOUSE_LEFT);
        if (down && !was_down) {
            dlg_cy = (uint8_t)(mouse_py >> 3);
            dlg_cx = (uint8_t)(mouse_px >> 3);
            /* Outside the dialog's live rows, a click is a cancel: it is
             * what clicking away from a modal box means everywhere, and it
             * gives "press any key" something to do as well. */
            if (hot_n && (dlg_cy < hot_y || dlg_cy >= (uint8_t)(hot_y + hot_n)))
                return K_ESC;
            return K_RETURN;
        }
        was_down = down;
    }
}

static void box(uint8_t y, uint8_t h, const char *title)
{
    uint8_t i;

    screen_fill(DLG_X, y, DLG_W, ' ', C_TITLE);
    screen_text(DLG_X + 2, y, title, (uint8_t)(DLG_W - 4), C_TITLE);
    for (i = 1; i < h; ++i)
        screen_fill(DLG_X, (uint8_t)(y + i), DLG_W, ' ', C_DLG);
}

void dlg_message(const char *title, const char *msg)
{
    box(8, 5, title);
    screen_text(DLG_X + 2, 10, msg, (uint8_t)(DLG_W - 4), C_DLG);
    screen_text(DLG_X + 2, 11, S_0, (uint8_t)(DLG_W - 4), C_DLG);
    hot_n = 0;                  /* no live rows: a click anywhere dismisses */
    dlg_wait();
}

/* Two buttons on their own row, so there is something to aim at. The
 * keyboard answer is unchanged -- Y is yes and anything else is no -- and
 * the buttons say so rather than replacing it. */
#define BTN_ROW 12
#define YES_X   ((uint8_t)(DLG_X + 2))
#define NO_X    ((uint8_t)(DLG_X + 14))
#define BTN_W   9

uint8_t dlg_confirm(const char *title, const char *msg)
{
    uint8_t k;

    box(8, 6, title);
    screen_text(DLG_X + 2, 10, msg, (uint8_t)(DLG_W - 4), C_DLG);
    screen_text(DLG_X + 2, 11, S_1,
                (uint8_t)(DLG_W - 4), C_DLG);
    screen_text(YES_X, BTN_ROW, S_yes, BTN_W, C_SEL);
    screen_text(NO_X,  BTN_ROW, S_no,  BTN_W, C_SEL);

    hot_y = BTN_ROW;
    hot_n = 1;
    k = dlg_wait();

    /* A click: yes only if it was on the Yes button. Anywhere else in the
     * row, or off the row entirely (which dlg_wait turns into ESC), is no
     * -- the safe answer for a question about discarding work. */
    if (dlg_cy != 0xFF)
        return (uint8_t)(k == K_RETURN && dlg_cx >= YES_X
                         && dlg_cx < (uint8_t)(YES_X + BTN_W));
    return k == 'y' || k == 'Y';
}

static char up(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

/* Case-insensitive check that `n` ends with `ext`. */
static uint8_t has_ext(const char *n, const char *ext)
{
    uint8_t nl = (uint8_t)strlen(n), el = (uint8_t)strlen(ext);
    const char *e;

    if (nl <= el)
        return 0;
    e = n + nl - el;
    while (*ext)
        if (up(*e++) != up(*ext++))
            return 0;
    return 1;
}

/* `exts` is one or more extensions packed end to end, each NUL-terminated,
 * and the LIST ends with an empty one -- so the literal needs a trailing
 * \0 of its own, on top of the one the compiler appends. Without it this
 * walks one byte past the array looking for the next entry.
 *
 * Import accepts both kinds and there is no sense offering a file it would
 * refuse. */
static uint8_t has_any_ext(const char *n, const char *exts)
{
    while (*exts) {
        if (has_ext(n, exts))
            return 1;
        while (*exts)
            ++exts;
        ++exts;
    }
    return 0;
}

static void scan(const char *ext)
{
    file_entry_t e;

    name_count = 0;
    names_h = bank_calloc(LIST_MAX * FILE_NAME_MAX);
    if (names_h == H_NULL)
        return;
    if (dir_open() != ERR_OK)
        return;
    while (name_count < LIST_MAX && dir_next(&e)) {
        if (e.is_dir || !has_any_ext(e.name, ext))
            continue;
        bank_write(names_h, (uint16_t)(name_count * FILE_NAME_MAX),
                   e.name, FILE_NAME_MAX);
        ++name_count;
    }
    dir_close();
}

/* Release the listing and hand back the caller's answer. */
static uint8_t finish(uint8_t r)
{
    bank_free(names_h);
    names_h = H_NULL;
    return r;
}

/* The words, packed: cc65 honours rodata-name for a named const array but
 * not for literals, so this is what keeps them out of resident memory. */
static const char result_words[] =
    "cells\0" "rows\0" "formulas\0" "more sheets skipped\0" "strings\0"
    "\0" "some did not fit\0" "some text changed\0" "failed\0"
    "some formulas kept as values\0";

static const char *word(uint8_t n)
{
    const char *p = result_words;

    while (n--) {
        while (*p)
            ++p;
        ++p;
    }
    return p;
}

static uint8_t put_num(char *out, uint8_t at, uint16_t v)
{
    char d[6];
    uint8_t k = 0;

    if (v == 0)
        d[k++] = '0';
    while (v) {
        d[k++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (k)
        out[at++] = d[--k];
    return at;
}

static uint8_t put_word(char *out, uint8_t at, const char *w)
{
    while (*w)
        out[at++] = *w++;
    return at;
}

void dlg_result(const char *title, uint16_t a, uint8_t a_word,
                uint16_t b, uint8_t b_word, uint8_t note)
{
    char msg[48];
    uint8_t n;

    n = put_num(msg, 0, a);
    msg[n++] = ' ';
    n = put_word(msg, n, word(a_word));

    if (b_word != 0xFF) {
        msg[n++] = ',';
        msg[n++] = ' ';
        n = put_num(msg, n, b);
        msg[n++] = ' ';
        n = put_word(msg, n, word(b_word));
    }

    if (note != RN_NONE) {
        msg[n++] = ' ';
        msg[n++] = '(';
        n = put_word(msg, n, word((uint8_t)(4 + note)));
        msg[n++] = ')';
    }

    msg[n] = '\0';
    dlg_message(title, msg);
}

uint8_t filedlg_open(const char *ext, char *name_out, uint8_t max)
{
    uint8_t sel = 0, i;

    scan(ext);

    if (name_count == 0) {
        dlg_message(S_2, S_3);
        return finish(0);
    }

    for (;;) {
        uint8_t k;

        box(4, (uint8_t)(name_count + 3), S_2);
        for (i = 0; i < name_count; ++i) {
            char row[FILE_NAME_MAX];
            name_get(i, row);
            screen_text(DLG_X + 2, (uint8_t)(5 + i), row,
                        (uint8_t)(DLG_W - 4), i == sel ? C_SEL : C_DLG);
        }
        screen_text(DLG_X + 2, (uint8_t)(5 + name_count),
                    S_4,
                    (uint8_t)(DLG_W - 4), C_DLG);

        hot_y = 5;
        hot_n = name_count;
        k = dlg_wait();

        if (dlg_cy != 0xFF && k == K_RETURN) {
            /* A click on the list. The first one on a row selects it and
             * the next one opens it: single-click-to-open would mean a
             * mis-aimed click loads a workbook over the one on screen. */
            uint8_t hit = (uint8_t)(dlg_cy - 5);
            if (hit != sel) {
                sel = hit;
                continue;
            }
        }

        if (k == K_UP && sel)
            --sel;
        else if (k == K_DOWN && sel + 1 < name_count)
            ++sel;
        else if (k == K_RETURN) {
            char row[FILE_NAME_MAX];
            name_get(sel, row);
            strncpy(name_out, row, max - 1);
            name_out[max - 1] = '\0';
            return 1;
        } else if (k == K_ESC)
            return 0;
    }
}

/* The Save-as dialog, generalised: a title, a line of guidance and a field.
 * Renaming a sheet wants exactly the same thing, and a second copy of the
 * editor loop would be the only difference between them. */
uint8_t dlg_prompt(const char *title, const char *hint, char *buf,
                   uint8_t max)
{
    editor_start(buf);

    for (;;) {
        ed_result_t r;
        uint8_t k;

        uint8_t caret = editor_caret();

        if (caret > FIELD_W - 1)        /* a name longer than the field */
            caret = FIELD_W - 1;

        box(8, 6, title);
        screen_text(DLG_X + 2, 10, S_7, (uint8_t)(sizeof S_7 - 1), C_DLG);
        screen_fill(FIELD_X, 10, FIELD_W, ' ', C_FIELD);
        screen_text(FIELD_X, 10, editor_text(), FIELD_W, C_FIELD);
        screen_recolor((uint8_t)(FIELD_X + caret), 10, 1, C_SEL);
        screen_text(DLG_X + 2, 12, hint, (uint8_t)(DLG_W - 4), C_DLG);
        screen_text(DLG_X + 2, 13, S_6, (uint8_t)(DLG_W - 4), C_DLG);

        /* The hint row is live: its left half is "RETURN accept" and its
         * right half "ESC cancel", so clicking the words does what they
         * say. Everything else still has to be typed -- a pointer cannot
         * fill in a filename. */
        hot_y = 13;
        hot_n = 1;
        k = dlg_wait();
        if (dlg_cy != 0xFF && k == K_RETURN && dlg_cx >= (uint8_t)(DLG_X + 16))
            k = K_ESC;

        r = editor_key(k);
        if (r == ED_CANCEL) {
            editor_stop();
            return 0;
        }
        if (r != ED_EDITING) {
            uint8_t n = editor_len();
            if (n == 0) {
                editor_stop();
                return 0;
            }
            if (n > max - 1)
                n = (uint8_t)(max - 1);
            memcpy(buf, editor_text(), n);
            buf[n] = '\0';
            editor_stop();
            return 1;
        }
    }
}

import_note_t file_note;

static const char S_x16s[] = ".X16S\0";
/* What Import can actually read. Must not be empty: an unfiltered listing
 * fills with overlay images before a spreadsheet appears. */
static const char S_any[]  = ".XLSX\0.CSV\0";
static const char S_imported[] = "Imported";

/* Say that something is happening, and roughly for how long.
 *
 * Opening a large workbook takes tens of seconds, most of it inflating the
 * worksheet, and a motionless file dialog reads as a hung machine.
 *
 * Drawn from this overlay, which has the room. It stays on screen by
 * itself: nothing repaints the grid until the command finishes and
 * after_file_op() runs, so no phase of the import has to preserve it.
 *
 * DELIBERATELY STATIC. A progress bar would have to be advanced from inside
 * each phase, and those phases live in the overlays with the least room in
 * the program. */
static const char S_busy1[] = "Working -- please wait.";
static const char S_busy2[] = "A large workbook can take a minute or two.";

void dlg_busy(const char *title, const char *name)
{
    /* Put the sheet back first. The file picker this replaces can be
     * thirty-five rows tall, and a six-row box drawn over it leaves the
     * rest of the list framing the message. grid_render_all() is resident,
     * so calling it from here is free and it is what the command would
     * have drawn on the way out anyway. */
    grid_render_all();
    box(8, 6, title);
    screen_text(DLG_X + 2, 9, name, (uint8_t)(DLG_W - 4), C_DLG);
    screen_text(DLG_X + 2, 11, S_busy1, (uint8_t)(DLG_W - 4), C_DLG);
    screen_text(DLG_X + 2, 12, S_busy2, (uint8_t)(DLG_W - 4), C_DLG);
}

uint8_t filedlg_ask(uint8_t cmd, char *name, uint8_t max)
{
    /* Anything that replaces the workbook asks first. */
    if ((cmd == FC_NEW || cmd == FC_OPEN) && wb_dirty
        && !dlg_discard(cmd == FC_NEW ? DC_NEW : DC_OPEN))
        return 0;

    switch (cmd) {
    case FC_NEW:
        return 1;                       /* nothing to name */
    case FC_OPEN:
        name[0] = '\0';
        if (!filedlg_open(S_x16s, name, max))
            return 0;
        dlg_busy(S_2, name);
        return 1;
    case FC_IMPORT:
        name[0] = '\0';
        if (!filedlg_open(S_any, name, max))
            return 0;
        dlg_busy(S_2, name);
        return 1;
    case FC_SAVE:
        /* A workbook that has been saved once keeps its name. */
        if (name[0]) {
            dlg_busy(S_5, name);
            return 1;
        }
        break;
    default:
        break;
    }
    if (!dlg_prompt(S_5, S_8, name, max))
        return 0;
    dlg_busy(S_5, name);
    return 1;
}

void filedlg_report(uint8_t cmd, err_t e)
{
    if (cmd == FC_IMPORT && file_note.is_xlsx) {
        dlg_xlsx_result(e);
        return;
    }
    if (e != ERR_OK) {
        dlg_failed(cmd == FC_OPEN   ? DF_OPEN :
                   cmd == FC_IMPORT ? DF_IMPORT :
                   cmd == FC_EXPORT ? DF_EXPORT : DF_SAVE, e);
        return;
    }
    if (cmd == FC_IMPORT)
        dlg_result(S_imported, file_note.rows, RW_ROWS,
                   file_note.cells, RW_CELLS,
                   file_note.truncated ? RN_TRUNCATED : RN_NONE);
}

uint8_t dlg_discard(uint8_t what)
{
    const char *p = S_discard_words;

    while (what--) {
        while (*p)
            ++p;
        ++p;
    }
    return dlg_confirm(p, S_discard);
}

uint8_t filedlg_save_as(char *name, uint8_t max)
{
    return dlg_prompt(S_5, S_8, name, max);
}

void dlg_failed(uint8_t what, err_t e)
{
    char title[16];
    const char *p = S_failed_words;
    uint8_t n = 0;

    while (what--) {
        while (*p)
            ++p;
        ++p;
    }
    while (*p)
        title[n++] = *p++;
    strcpy(title + n, S_failed);
    dlg_message(title, err_message(e));
}

/* Which two counters matter depends on what the workbook turned out to be:
 * a multi-sheet workbook's headline is how many sheets were left behind,
 * and a single-sheet one's is how many formulas came across. */
void dlg_xlsx_result(err_t e)
{
    const xlsx_report_t *rep = xlsx_result();
    char tabname[WB_TAB_NAME];

    /* A failure that still landed cells is worth reporting as a result
     * rather than an error: the user has most of their workbook. */
    if (e != ERR_OK && rep->cells == 0) {
        dlg_failed(DF_IMPORT, e);
        return;
    }
    wb_tab_name(0, tabname);
    dlg_result(tabname,
               rep->cells, RW_CELLS,
               rep->sheets_skipped ? rep->sheets_skipped : rep->formulas,
               rep->sheets_skipped ? RW_SHEETS : RW_FORMULAS,
               rep->truncated ? RN_TRUNCATED :
               rep->lossy     ? RN_CHANGED :
               /* Worth saying even when nothing went wrong: those cells look
                * right and are not going to stay right. */
               rep->formulas_live < rep->formulas ? RN_STALE : RN_NONE);
}

/* --- the sheet commands ------------------------------------------------
 *
 * Here rather than in menu.c, which is where the menu that invokes them
 * lives, because each one asks something first and the asking is in this
 * overlay. An overlay calling into another jumps to whatever is loaded at
 * that address; renaming a sheet hung the machine until this moved.
 */

static const char S_addtitle[] = "New sheet";
static const char S_rentitle[] = "Rename sheet";
static const char S_prompt[]   = "name:";
static const char S_deltitle[] = "Delete sheet";
static const char S_delask[]   = "delete it and its cells?";
static const char S_sheetfail[] = "Sheet";

/* --- sorting: the asking half --------------------------------------- *
 *
 * The work is in sort_ovl.c, in OVL_CSV, because it came to about 1800
 * bytes and this overlay had 180. What stays here is everything that talks
 * to the user, because the dialogs are here and an overlay cannot call
 * another overlay.
 *
 * So this checks what can be checked, asks what needs asking, and hands
 * back the banked index block the sort will need -- allocated HERE so that
 * running out of memory is reported by a dialog rather than discovered in
 * an overlay that has no way to say so. H_NULL means do not sort.
 */
static const char S_sorttitle[] = "Sort";
/* Relative references move with a sorted formula, so the warning is not
 * about formulas in general. What remains is the case that cannot be
 * expressed at all: a range naming rows a sort scatters is not a range
 * afterwards. */
static const char S_sortformulas[] = "ranges over sorted rows may be wrong";
static const char S_sortbig[] = "too many rows to sort";
static const char S_sortmem[] = "not enough memory to sort";

/* Sorting runs from the CURSOR'S ROW to the last used one, not from row 1,
 * so a heading row is left where it is. The user says where the data
 * starts by putting the cursor on it -- the arrangement VisiCalc and Lotus
 * used, and one rule with "sorts by the column the cursor is in".
 *
 * A trailing totals row is still swept in with the data; nothing here can
 * know it is special. */
/* Ask about sorting, then build the permutation the sort will realise.
 *
 * Returns the index in banked RAM, or H_NULL if the user declined or there
 * is nothing to sort. */
handle_t sort_ask(void)
{
    const cellstore_t *cs = wb_cells();
    cell_record_t rec;
    uint16_t base = sort_base();
    uint16_t n, r, c, formulas = 0;
    handle_t idx;

    if (cs->cell_count == 0 || base >= cs->max_row)
        return H_NULL;                  /* nothing below the cursor */

    n = (uint16_t)(cs->max_row - base + 1);
    if (n > BANK_SIZE / 2) {
        dlg_message(S_sorttitle, S_sortbig);
        return H_NULL;
    }

    for (r = base; r <= cs->max_row && !formulas; ++r)
        for (c = 0; c <= cs->max_col; ++c)
            if (cells_get(cs, r, c, &rec) && rec.type == CELL_FORMULA) {
                ++formulas;
                break;
            }

    if (formulas && !dlg_confirm(S_sorttitle, S_sortformulas))
        return H_NULL;

    idx = bank_alloc((uint16_t)(n * 2));
    if (idx == H_NULL)
        dlg_message(S_sorttitle, S_sortmem);
    return idx;
}

void sheet_command(uint8_t cmd)
{
    {
    char name[X16S_MAX_SHEET_NAME + 1];
    err_t e;

    if (cmd == MENU_SHEET_DEL) {
        wb_sheet_name(wb_sheet_i, name);
        if (!dlg_confirm(S_deltitle, S_delask))
            return;
        e = wb_sheet_delete(wb_sheet_i);
        if (e != ERR_OK)
            dlg_message(S_sheetfail, err_message(e));
        return;
    }

    /* Add and rename both want a name. Rename starts from the current one
     * so a small correction does not mean retyping it. */
    if (cmd == MENU_SHEET_REN)
        wb_sheet_name(wb_sheet_i, name);
    else
        name[0] = '\0';

    if (!dlg_prompt(cmd == MENU_SHEET_ADD ? S_addtitle : S_rentitle,
                    S_prompt, name, sizeof name))
        return;

    e = (cmd == MENU_SHEET_ADD) ? wb_sheet_add(name)
                                : wb_sheet_rename(wb_sheet_i, name);
    if (e != ERR_OK)
        dlg_message(S_sheetfail, err_message(e));
    }
}

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
