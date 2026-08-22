/* menu.c — see menu.h. */
#include "menu.h"
#include "screen.h"
#include "grid.h"
#include "../platform/keyboard.h"
#include "../platform/mouse.h"
#include "filedlg.h"
#include "../workbook/workbook.h"
#include "../workbook/cells.h"
#include "../workbook/strings.h"
#include "../workbook/styles.h"
#include "../platform/banked_ram.h"
#include "../formula/formula.h"
#include "../util/number.h"
#include "grid.h"
#include "../util/errors.h"
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY14")
#  pragma rodata-name (push, "OVL14RO")
#  pragma bss-name (push, "OVL14BSS")
#endif

/* One item of a dropdown.
 *
 * `key` is the code menu_run() returns when the item is chosen, and for
 * anything with a keyboard shortcut it IS that key -- the same code
 * grid_key() answers to from the worksheet, so the label doubles as the
 * reference for it. Commands with no shortcut use a MENU_* code from
 * menu.h instead. */
typedef struct {
    const char *label;
    uint8_t     key;
} item_t;

/* Named arrays, not literals in the initialisers. cc65 puts a string
 * literal in resident RODATA however rodata-name is set, and these labels
 * came to 246 bytes of it — every byte of which the resident core cannot
 * spare, for text only this overlay ever draws. */
static const char L_new[]  = "New        F1";
static const char L_open[] = "Open       F3";
static const char L_save[] = "Save       F4";
static const char L_sas[]  = "Save as    F6";
static const char L_imp[]  = "Import     F7";
static const char L_exp[]  = "Export     F8";
static const char L_quit[] = "Quit       F9";
static const char L_edit[] = "Edit cell  F2";
static const char L_snext[] = "Next";
static const char L_sprev[] = "Previous";
static const char L_sadd[]  = "Add...";
static const char L_sren[]  = "Rename...";
static const char L_sdel[]  = "Delete";
static const char L_sortu[] = "Sort up";
static const char L_sortd[] = "Sort down";
static const char L_sortu2[] = "Undo sort";
static const char T_sheet[] = "Sheet";
static const char L_sel[]  = "Select     ^A";
static const char L_copy[] = "Copy       ^C";
static const char L_paste[] = "Paste      ^V";
static const char L_undo[] = "Undo       F5";
static const char L_clr[]  = "Clear     DEL";
static const char L_find[] = "Find      ^F";
static const char L_rep[]  = "Replace...";
static const char L_insrow[] = "Insert row";
static const char L_delrow[] = "Delete row";
static const char L_inscol[] = "Insert column";
static const char L_delcol[] = "Delete column";
static const char L_calcn[] = "Recalc now";
static const char L_calca[] = "Auto recalc";
static const char L_frz[] = "Freeze";
static const char L_colw[] = "Column width";
static const char L_bold[] = "Bold      ^B";
static const char L_chart[] = "Bars";
static const char L_chartl[] = "Line";
static const char L_chartp[] = "Pie";
static const char T_file[] = "File";
static const char T_edit[] = "Edit";
static const char T_data[] = "Data";
static const char T_chart[] = "Chart";
static const char T_layout[] = "Layout";

static const item_t file_items[] = {
    { L_new,  0x85 },
    { L_open, 0x86 },
    { L_save, 0x8A },
    { L_sas,  0x8B },
    { L_imp,  0x88 },
    { L_exp,  0x8C },
    { L_quit, 0x10 },
    { 0, 0 }
};

static const item_t sheet_items[] = {
    { L_snext, MENU_SHEET_NEXT },
    { L_sprev, MENU_SHEET_PREV },
    { L_sadd,  MENU_SHEET_ADD  },
    { L_sren,  MENU_SHEET_REN  },
    { L_sdel,  MENU_SHEET_DEL  },
    { 0, 0 }
};

static const item_t edit_items[] = {
    { L_edit,  0x89 },
    { L_sel,   0x01 },      /* a real key, like Copy and Paste below */
    { L_copy,  0x03 },      /* the codes grid_key answers to, not stand-ins: */
    { L_paste, 0x16 },      /* these two are real keys, unlike the sheet ones */
    { L_undo,  0x87 },
    { L_clr,   0x14 },
    { L_find,  0x06 },
    { L_rep,   MENU_REPLACE },
    { 0, 0 }
};

/* The shape of the grid, as against what is typed into it (Edit) or which
 * sheet is on screen (Sheet).
 *
 * Edit had twelve items against three to seven everywhere else, and half of
 * them were not editing at all. Freeze and Column width came the other way,
 * out of Sheet: they describe the grid, not which of the sheets it shows,
 * and Column width belongs beside Insert column rather than two menus away
 * from it. */
static const item_t layout_items[] = {
    { L_insrow, MENU_INS_ROW },
    { L_delrow, MENU_DEL_ROW },
    { L_inscol, MENU_INS_COL },
    { L_delcol, MENU_DEL_COL },
    { L_colw,   MENU_COL_W   },
    { L_bold,   MENU_BOLD    },
    { L_frz,    MENU_FREEZE  },
    { 0, 0 }
};

/* Sorting and recalculation: what is done TO the numbers, as against the
 * sheet that holds them or the cells being typed into. */
static const item_t data_items[] = {
    { L_sortu,  MENU_SORT_ASC   },
    { L_sortd,  MENU_SORT_DESC  },
    { L_sortu2, MENU_SORT_UNDO  },
    { L_calcn,  MENU_CALC_NOW   },
    { L_calca,  MENU_CALC_AUTO  },
    { 0, 0 }
};

static const item_t chart_items[] = {
    { L_chart,  MENU_CHART      },
    { L_chartl, MENU_CHART_LINE },
    { L_chartp, MENU_CHART_PIE  },
    { 0, 0 }
};

typedef struct {
    const char     *title;
    uint8_t         x;          /* where its label starts on the bar */
    const item_t   *items;
} menu_t;

/* The x of each title has to match S_bar below, character for character.
 * Nothing checks that for you. */
static const menu_t menus[] = {
    { T_file,    1, file_items   },
    { T_edit,    7, edit_items   },
    { T_layout, 13, layout_items },
    { T_data,   21, data_items   },
    { T_chart,  27, chart_items  },
    { T_sheet,  34, sheet_items  }
};
#define MENU_COUNT 6

/* The bar as drawn while a menu is open. The closed version is in grid.c,
 * which paints it on every render and cannot call into here to ask — an
 * overlay load per screen redraw would be absurd. The two must agree about
 * which menus exist; there is no way to make the compiler check that, so
 * they carry each other's names in a comment instead. */
static const char S_bar[] = " File  Edit  Layout  Data  Chart  Sheet";

/* The bar is the frame's grey, so opening a menu does not repaint the top
 * of the screen a different colour. The PANEL is white -- brighter than
 * the frame, not the same as it.
 *
 * It was the frame's grey too, and a test caught the problem: a dropdown
 * lands on the column-header row, which is also part of the frame, so the
 * panel's edge disappeared into it exactly where the eye needs to see
 * where the menu stops. See the palette note in grid.c. */
#define C_BAR    COLOR(COL_BLACK, COL_LGREY)
#define C_OPEN   COLOR(COL_BLACK, COL_YELLOW)
/* A shade darker than either the frame or the paper, because it drops over
 * both. This has now been wrong in both directions and the same assertion
 * caught it each time: grey when the cells were black, so the panel
 * dissolved into the column-header row; white when the cells went white,
 * so it dissolved into them. The test is about clipping and keeps turning
 * out to be about the palette. */
#define C_ITEM   COLOR(COL_WHITE, COL_MGREY)
#define C_PICKED COLOR(COL_BLACK, COL_YELLOW)

/* A dropdown is as wide as its widest label and as tall as its item list.
 *
 * Erasing the previous panel is done by repainting the grid underneath, and
 * only when the OPEN MENU CHANGES -- moving the highlight within one menu
 * redraws the same rectangle and needs no erase. Without that, moving from
 * a long menu to a short one leaves the tail of the longer list behind. */
#define ITEM_PAD 2      /* one space either side of the longest label */

static uint8_t count_of(const item_t *it)
{
    uint8_t n = 0;

    while (it[n].label)
        ++n;
    return n;
}

/* How wide this menu's panel has to be: its longest label, plus a space at
 * each side. Measured rather than tabulated -- the labels are right here
 * and a second table saying how long they are is a second thing to keep in
 * step with them. */
static uint8_t width_of(const item_t *it)
{
    uint8_t w = 0, i, l;

    for (i = 0; it[i].label; ++i) {
        for (l = 0; it[i].label[l]; ++l)
            ;
        if (l > w)
            w = l;
    }
    return (uint8_t)(w + ITEM_PAD);
}

static void draw(uint8_t m, uint8_t sel, uint8_t erase)
{
    const menu_t *mu = &menus[m];
    uint8_t n = count_of(mu->items), i;
    uint8_t w = width_of(mu->items);

    /* Whatever the last panel covered, put back. Only on a change of menu:
     * within one, every draw covers exactly what the last one did. */
    if (erase)
        grid_render_all();

    /* The bar, with the open menu inverted so it is obvious which list
     * belongs to which title. */
    screen_text(0, 0, S_bar, screen_cols(), C_BAR);
    screen_text(mu->x, 0, mu->title, 4, C_OPEN);

    for (i = 0; i < n; ++i) {
        color_t c = (i == sel) ? C_PICKED : C_ITEM;

        screen_fill(mu->x, (uint8_t)(i + 1), w, ' ', c);
        screen_text((uint8_t)(mu->x + 1), (uint8_t)(i + 1),
                    mu->items[i].label, (uint8_t)(w - 1), c);
    }
}

/* Which menu a column on the bar belongs to: the last one whose title
 * starts at or before it. That gives each title the gap after it as well,
 * so the two spaces between "File" and "Edit" pick File rather than
 * nothing, and anything past "Sheet" picks Sheet. A click on the bar always
 * lands on some menu, which is what makes it feel like a bar rather than
 * three small targets. */
static uint8_t menu_at(uint8_t sx)
{
    uint8_t m = 0, i;

    for (i = 1; i < MENU_COUNT; ++i)
        if (sx >= menus[i].x)
            m = i;
    return m;
}

/* --- about ------------------------------------------------------------
 *
 * The product name on the menu bar is a button, and this is what it opens.
 * Drawn here rather than through the dialogs in OVL_FILEDLG, which have 56
 * bytes free and would need loading; this overlay is already resident when
 * the bar is clicked.
 *
 * The version comes from the Makefile through -DCC_VERSION, so the box
 * and `make release` cannot disagree about what this build is.
 */
#ifndef CC_VERSION
#  define CC_VERSION "dev"
#endif

#define AB_W 40
#define AB_H 9

static const char S_ab1[] = "Commander Calc " CC_VERSION;
static const char S_ab2[] = "A spreadsheet for the Commander X16";
static const char S_ab3[] = "65C02 . VERA . CMDR-DOS";
static const char S_ab4[] = "Press any key";

static void about_run(void)
{
    uint8_t x = (uint8_t)((screen_cols() - AB_W) >> 1);
    uint8_t y = (uint8_t)((screen_rows() - AB_H) >> 1);
    uint8_t i;

    screen_fill(x, y, AB_W, ' ', C_OPEN);
    screen_text((uint8_t)(x + 2), y, S_ab1,
                (uint8_t)(AB_W - 4), C_OPEN);
    for (i = 1; i < AB_H; ++i)
        screen_fill(x, (uint8_t)(y + i), AB_W, ' ', C_ITEM);

    screen_text((uint8_t)(x + 2), (uint8_t)(y + 2), S_ab2,
                (uint8_t)(AB_W - 4), C_ITEM);
    screen_text((uint8_t)(x + 2), (uint8_t)(y + 3), S_ab3,
                (uint8_t)(AB_W - 4), C_ITEM);
    screen_text((uint8_t)(x + 2), (uint8_t)(y + 6), S_ab4,
                (uint8_t)(AB_W - 4), C_ITEM);

    kbd_wait();
}

uint8_t menu_run(void)
{
    uint8_t m = 0, sel = 0, redraw = 1;
    /* The press that opened the bar is in all likelihood still held: the
     * grid acts on the edge and comes straight here. Starting as though it
     * were already seen stops that same press being read a second time as a
     * click on whatever the pointer happens to be over. */
    uint8_t was_down = 1;
    /* Which menu is on screen, so a redraw knows whether it has another
     * one's panel to clear away first. Nothing is drawn yet. */
    uint8_t drawn_m;

    if (menu_open_x == MENU_ABOUT) {
        menu_open_x = MENU_BY_KEY;
        about_run();
        return 0;                       /* no command; the grid repaints */
    }
    if (menu_open_x != MENU_BY_KEY)
        m = menu_at(menu_open_x);
    menu_open_x = MENU_BY_KEY;

    /* The grid is intact when the bar opens -- the caller has just been
     * rendering it -- so the first draw has nothing to erase. Saying the
     * first menu is already drawn is how it skips that repaint. */
    drawn_m = m;

    for (;;) {
        uint8_t k;

        if (redraw) {
            draw(m, sel, (uint8_t)(m != drawn_m));
            drawn_m = m;
            redraw = 0;
        }

        /* Poll rather than wait. kbd_wait() blocks, and a menu that cannot
         * see the pointer while it is open is a menu that can only be
         * opened with the mouse and not used with it. */
        k = kbd_get();
#ifdef X16S_HOST
        /* No pointer to wait for off the machine, and mouse_host.c never
         * reports one moving, so an empty key queue here would spin for
         * ever. Cancel instead -- the same thing kbd_wait() did by handing
         * back ESC, and what an unscripted dialog should do. */
        if (!k)
            return 0;
#endif
        if (!k) {
            uint8_t sx, sy, down, n;

            mouse_poll();               /* first: the rest reads its results */
            sx = (uint8_t)(mouse_px >> 3);
            sy = (uint8_t)(mouse_py >> 3);
            down = (uint8_t)(mouse_btn & MOUSE_LEFT);
            n = count_of(menus[m].items);

            if (sy == 0) {
                /* Along the bar: sliding across it opens each menu in turn,
                 * the way a dropdown behaves everywhere else. */
                uint8_t t = menu_at(sx);
                if (t != m) {
                    m = t;
                    sel = 0;
                    redraw = 1;
                }
            } else if (sy <= n && sx >= menus[m].x
                       && sx < (uint8_t)(menus[m].x
                                         + width_of(menus[m].items))) {
                /* Over an item: highlight it, and take it on a click. */
                if (sel != (uint8_t)(sy - 1)) {
                    sel = (uint8_t)(sy - 1);
                    redraw = 1;
                }
                if (down && !was_down)
                    return menus[m].items[sel].key;
            } else if (down && !was_down) {
                return 0;               /* clicked off the menu: close it */
            }

            was_down = down;
            continue;
        }

        switch (k) {
        case 0x9D:                              /* left */
            m = (uint8_t)(m ? m - 1 : MENU_COUNT - 1);
            sel = 0;
            break;
        case 0x1D:                              /* right */
            m = (uint8_t)((m + 1) % MENU_COUNT);
            sel = 0;
            break;
        case 0x91:                              /* up */
            {
                uint8_t n = count_of(menus[m].items);
                sel = (uint8_t)(sel ? sel - 1 : n - 1);
            }
            break;
        case 0x11:                              /* down */
            sel = (uint8_t)((sel + 1) % count_of(menus[m].items));
            break;
        case 0x0D:                              /* RETURN: take it */
            return menus[m].items[sel].key;
        case 0x1B:                              /* ESC */
        case 0x15:                              /* F10 again: closes */
            return 0;
        default:
            /* Any command key still works with the menu open, which means
             * F7 does the same thing whether or not someone opened this
             * first. Passing it back rather than swallowing it is free. */
            if (k >= 0x85 || k == 0x10 || k == 0x14)
                return k;
            break;
        }

        /* Every case above that does not return has moved the highlight or
         * the open menu. */
        redraw = 1;
    }
}

/* --- find ------------------------------------------------------------- *
 *
 * Walks the used range for the text find_ask() collected, and puts the
 * cursor on the first cell containing it, ignoring case.
 *
 * In the menu's overlay rather than the dialogs': everything it touches is
 * resident, and when Find is chosen from the Edit menu this overlay is
 * already loaded.
 *
 * No "not found" message -- that would need a dialog, and the dialogs are
 * in another overlay. The cursor not moving is the answer.
 *
 * One pass, remembering the first match anywhere: a match after the cursor
 * wins immediately, and failing that the remembered one is the wrap. That
 * gives "find next" without storing a position, so repeating the command
 * walks the matches.
 */
static char find_cell[32];      /* as far into a cell as a search looks */

/* Find asks for its own text rather than borrowing dlg_prompt.
 *
 * dlg_prompt is in OVL_FILEDLG, and reaching it meant loading that overlay
 * to ask and this one to look -- two loads, and a resident dispatch that
 * did not fit in the bytes there were. screen_text and kbd_wait are
 * resident, so a one-line prompt costs this overlay a little and the
 * resident core nothing at all. It reads like the status line because that
 * is where it is. */
static const char S_findp[] = "Find: ";
static const char S_repp[]  = "Replace with: ";
static const char S_repq[]  = "Replace? Y/N/A, Esc to stop";

/* Wipe the bottom line. Whatever this overlay asked there stays on screen
 * otherwise: grid_goto() repaints cells and headers, not the message row,
 * so "Find: 11" sat under the sheet with the search long finished and the
 * next Return read as an ordinary cursor move. */
static void unask(void)
{
    screen_fill(0, (uint8_t)(screen_rows() - 1), screen_cols(), ' ',
                COLOR(COL_BLACK, COL_WHITE));
}

/* The bottom line, in the editor's colours, for anything this overlay has
 * to ask. It cannot reach the dialogs in OVL_FILEDLG at all. */
#define C_ASK COLOR(COL_BLACK, COL_YELLOW)
#define C_FIND C_ASK

/* Ask for a line of text on the bottom row, starting from whatever is
 * already in `buf`. Returns 0 for Escape, or for Return on an empty line
 * when `allow_empty` is 0 -- Find has nothing to look for then, where
 * Replace legitimately replaces with nothing.
 *
 * `max` counts the NUL. Shared by Find and Replace: they had the same
 * twenty lines and the second one would have been the third place this
 * program grew its own text entry. */
static uint8_t ask_text(const char *prompt, uint8_t plen, char *buf,
                        uint8_t max, uint8_t allow_empty)
{
    uint8_t y = (uint8_t)(screen_rows() - 1);
    uint8_t n = 0, k;

    while (buf[n])
        ++n;

    for (;;) {
        screen_fill(0, y, screen_cols(), ' ', C_FIND);
        screen_text(0, y, prompt, plen, C_FIND);
        screen_text(plen, y, buf, (uint8_t)(max - 1), C_FIND);
        screen_put((uint8_t)(plen + n), y, '_', C_FIND);

        k = kbd_wait();
        if (k == 13)                     /* Return */
            return (uint8_t)(n != 0 || allow_empty);
        if (k == 27)                     /* Escape: never mind */
            return 0;
        if (k == 20) {                   /* Backspace */
            if (n)
                buf[--n] = 0;
            continue;
        }
        if (k >= 32 && k < 127 && n < (uint8_t)(max - 1)) {
            buf[n++] = (char)k;
            buf[n] = 0;
        }
    }
}

static uint8_t find_ask(void)
{
    return ask_text(S_findp, 6, find_text, sizeof find_text, 0);
}

static char up(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

static uint8_t contains(const char *hay, const char *needle)
{
    uint8_t i, j;

    if (!needle[0])
        return 0;
    for (i = 0; hay[i]; ++i) {
        for (j = 0; needle[j]; ++j)
            if (up(hay[i + j]) != up(needle[j]))
                break;
        if (!needle[j])
            return 1;
    }
    return 0;
}

void find_run(void)
{
    const cellstore_t *cs = wb_cells();
    const grid_t *g = grid_state();
    uint16_t r, c, br = 0, bc = 0;
    uint8_t found = 0;

    /* unask() on EVERY way out, cancelling included, or the prompt is left
     * on the message row with nothing running behind it. */
    if (!find_ask() || cs->cell_count == 0) {
        unask();
        return;
    }

    for (r = 0; r <= cs->max_row; ++r) {
        for (c = 0; c <= cs->max_col; ++c) {
            wb_display_text(r, c, find_cell, sizeof find_cell);
            if (!contains(find_cell, find_text))
                continue;
            if (r > g->cur_row || (r == g->cur_row && c > g->cur_col)) {
                unask();
                grid_goto(r, c);
                return;
            }
            if (!found) {
                br = r; bc = c; found = 1;
            }
        }
    }

    unask();
    if (found)
        grid_goto(br, bc);
}


/* --- inserting and deleting rows --------------------------------------- *
 *
 * Whole rows, every column of the used range, from the cursor's row down.
 * Insert pushes everything below the cursor one row further down and
 * leaves an empty row where the cursor is; delete takes the cursor's row
 * away and pulls the rest up.
 *
 * Records move without their ownership moving with them: cells_set and
 * cells_delete leave reference counts alone, so a cell changes address and
 * nothing else. Sorting relies on the same property, and it is why nothing
 * here can leak a string or free one twice.
 *
 * Formula references are adjusted afterwards, not here: this sets
 * row_shift_by and the grid runs the rewriter, which lives in OVL_FREF and
 * cannot be called from this overlay.
 *
 * Deleting asks first, because it destroys a row and there is no undo for
 * it. Inserting does not -- the way back is to delete the row again.
 */
static const char S_delrow[] = "Delete row? (y/n)";

/* Move one row onto another, clearing the destination where the source is
 * empty -- otherwise the row being written over shows through in the
 * columns the moving row does not use. */
static void row_move(uint16_t from, uint16_t to, uint16_t cols)
{
    cellstore_t *cs = (cellstore_t *)wb_cells();
    cell_record_t rec;
    uint16_t c;

    for (c = 0; c < cols; ++c) {
        if (cells_get(cs, from, c, &rec)) {
            cells_delete(cs, from, c);
            rec.col = (uint8_t)c;
            cells_set(cs, to, c, &rec);
        } else {
            cells_delete(cs, to, c);
        }
    }
}

static void row_clear(uint16_t row, uint16_t cols)
{
    uint16_t c;

    for (c = 0; c < cols; ++c)
        cells_delete((cellstore_t *)wb_cells(), row, c);
}

static uint8_t row_cmd(uint8_t del)
{
    const cellstore_t *cs = wb_cells();
    uint16_t at = grid_state()->cur_row;
    uint16_t last = cs->max_row, cols, r;

    /* Below the last used row there is nothing to push down and nothing to
     * take away, so both commands are a no-op there. */
    if (cs->cell_count == 0 || at > last)
        return 0;
    cols = (uint16_t)(cs->max_col + 1);

    if (del) {
        uint8_t y = (uint8_t)(screen_rows() - 1);

        screen_fill(0, y, screen_cols(), ' ', C_ASK);
        screen_text(0, y, S_delrow, (uint8_t)(sizeof S_delrow - 1), C_ASK);
        if (up((char)kbd_wait()) != 'Y')
            return 0;

        for (r = at; r < last; ++r)
            row_move((uint16_t)(r + 1), r, cols);
        row_clear(last, cols);
    } else {
        if (last >= X16S_MAX_ROWS - 1)
            return 0;                   /* nowhere for the last row to go */
        for (r = (uint16_t)(last + 1); r > at; --r)
            row_move((uint16_t)(r - 1), r, cols);
        /* row_move emptied the cursor's row on its way past. */
    }

    /* So the grid can adjust the formulas: it has to load another overlay
     * to do that, and this is one. */
    row_shift_at = at;
    row_shift_by = del ? -1 : 1;

    wb_dirty = 1;
    return 1;
}

/* --- column width -----------------------------------------------------
 *
 * The one thing on the list of what VisiCalc had and this did not that was
 * still missing. wb_set_col_width() has been there all along -- but until
 * now only the two importers called it, so a width could arrive from a
 * file and never be changed, and a sheet typed from scratch had every
 * column ten characters wide forever.
 *
 * Asks on the bottom line, the way Find does and for the same reason: the
 * dialogs are in OVL_FILEDLG and this overlay cannot reach them. It starts
 * with the current width already typed, so Return alone is a no-op and a
 * digit or two is the whole interaction.
 *
 * wb_set_col_width() clamps to X16S_MIN_COL_W..X16S_MAX_COL_W itself, so
 * nothing here has to know what those are. Zero means cancel, which is
 * below the minimum and so could never be meant.
 */
static const char S_colwp[] = "Width: ";

static void col_width_run(void)
{
    uint8_t y = (uint8_t)(screen_rows() - 1);
    uint16_t col = grid_state()->cur_col;
    char buf[4];
    uint8_t n = 0, k, w = wb_col_width(col);

    /* The current width, as digits, to be typed over or added to. */
    if (w >= 10) {
        uint8_t t = 0;
        while (w >= 10) { w = (uint8_t)(w - 10); ++t; }
        buf[n++] = (char)('0' + t);
    }
    buf[n++] = (char)('0' + w);
    buf[n] = 0;

    for (;;) {
        screen_fill(0, y, screen_cols(), ' ', C_ASK);
        screen_text(0, y, S_colwp, (uint8_t)(sizeof S_colwp - 1), C_ASK);
        screen_text(7, y, buf, n, C_ASK);
        screen_put((uint8_t)(7 + n), y, '_', C_ASK);

        k = kbd_wait();
        if (k == 27)                            /* Escape */
            break;
        if (k == 13) {                          /* Return: apply it */
            uint8_t i, v = 0;

            for (i = 0; i < n; ++i)
                v = (uint8_t)(v * 10 + (uint8_t)(buf[i] - '0'));
            if (v)
                wb_set_col_width(col, v);
            break;
        }
        if (k == 20) {                          /* Backspace */
            if (n)
                buf[--n] = 0;
            continue;
        }
        if (k >= '0' && k <= '9' && n < sizeof buf - 1) {
            buf[n++] = (char)k;
            buf[n] = 0;
        }
    }
    grid_render_all();
}

/* --- replace -----------------------------------------------------------
 *
 * Find, then change what it found. Asks per cell rather than doing the lot
 * silently, because there is no undo for it; A answers for every remaining
 * cell.
 *
 * FORMULA CELLS ARE SKIPPED. Replacing inside one would mean rewriting its
 * source, which a careless search would destroy. Numbers and text are both
 * matched against what the cell displays.
 *
 * A cell longer than the buffer is skipped rather than truncated, the same
 * rule the formula rewriter uses.
 */
/* rep_text lives in grid.c, in golden RAM, beside find_text -- see there
 * for why it cannot live here. */
static char rep_in[WB_TEXT_MAX];
static char rep_out[WB_TEXT_MAX];

/* Build `rep_in` with every occurrence of find_text swapped for rep_text.
 * 0 if it would not fit, in which case rep_out is meaningless. */
static uint8_t rep_build(void)
{
    uint8_t i = 0, o = 0, j;

    while (rep_in[i]) {
        for (j = 0; find_text[j]; ++j)
            if (up(rep_in[i + j]) != up(find_text[j]))
                break;

        if (find_text[j]) {                     /* no match here */
            if (o >= (uint8_t)(sizeof rep_out - 1))
                return 0;
            rep_out[o++] = rep_in[i++];
            continue;
        }
        for (j = 0; rep_text[j]; ++j) {
            if (o >= (uint8_t)(sizeof rep_out - 1))
                return 0;
            rep_out[o++] = rep_text[j];
        }
        for (j = 0; find_text[j]; ++j)          /* step over the match */
            ++i;
    }
    rep_out[o] = 0;
    return 1;
}

/* NOT wb_set_text(). That ends in wb_after_change(), which loads OVL_FEVAL
 * -- over this overlay, whose return address then points into code that no
 * longer exists. check_overlays.py caught it; the machine would have hung.
 *
 * So the record is built here, the way row_cmd() moves cells here, and the
 * recalculation is the grid's job once this has returned to resident code.
 * strpool_add and strpool_unref are resident and load nothing.
 *
 * The old string is released AFTER the new one is stored, so a failure to
 * allocate leaves the cell exactly as it was rather than pointing at a
 * freed id. */
static uint8_t put_text(uint16_t r, uint16_t c, cell_record_t *rec,
                        const char *text);

/* Release whatever a cell owns, then delete it. wb_clear() would do this
 * and then recalculate, which loads OVL_FEVAL over this overlay -- so the
 * releasing is spelled out here, as it is in every other overlay that
 * rewrites cells. */
static void cell_drop(uint16_t r, uint16_t c)
{
    cell_record_t rec;

    if (!cells_get(wb_cells(), r, c, &rec))
        return;
    if (rec.type == CELL_TEXT) {
        uint16_t id;
        memcpy(&id, rec.val, 2);
        strpool_unref(id);
    } else if (rec.type == CELL_FORMULA) {
        handle_t fr;
        memcpy(&fr, rec.val, sizeof fr);
        if (fr) {
            strpool_unref(bank_peek16(fr, FR_SRC));
            bank_free(fr);
        }
    }
    cells_delete((cellstore_t *)wb_cells(), r, c);
    wb_dirty = 1;
}

/* wb_set_text() for everything EXCEPT a formula, without the recalculation
 * that makes wb_set_text() unusable from an overlay. Same reading of the
 * text: blank clears, TRUE/FALSE is a boolean, anything that parses is a
 * number, the rest is a label. A leading '=' is refused here and left to
 * OVL_FCOMPILE, which is the only overlay that may compile one. */
static uint8_t put_value(uint16_t r, uint16_t c, const char *text,
                         uint8_t style)
{
    cell_record_t rec;
    snum_t v;

    while (*text == ' ')
        ++text;
    if (*text == '\0') {
        cell_drop(r, c);
        return 1;
    }
    if (*text == '=')
        return 0;

    cell_drop(r, c);
    memset(&rec, 0, sizeof rec);
    rec.col = (uint8_t)c;
    rec.style = style;

    if (up(text[0]) == 'T' && up(text[1]) == 'R') {
        rec.type = CELL_BOOLEAN; rec.val[0] = 1;
    } else if (up(text[0]) == 'F' && up(text[1]) == 'A') {
        rec.type = CELL_BOOLEAN; rec.val[0] = 0;
    } else if (snum_parse(text, &v) == ERR_OK) {
        rec.type = CELL_NUMBER;
        memcpy(rec.val, v.b, 5);
    } else {
        /* NOT put_text(): that releases the string the record used to
         * hold, and cell_drop() above has already done it. Pool id 0 is a
         * real id, so a zeroed record cannot be told from one holding
         * string 0 -- the only safe answer is not to ask. */
        uint16_t id, len = 0;

        while (text[len])
            ++len;
        if (strpool_add(text, len, &id) != ERR_OK)
            return 0;
        rec.type = CELL_TEXT;
        rec.val[0] = (uint8_t)id;
        rec.val[1] = (uint8_t)(id >> 8);
    }

    cells_set((cellstore_t *)wb_cells(), r, c, &rec);
    wb_dirty = 1;
    return 1;
}

static uint8_t put_text(uint16_t r, uint16_t c, cell_record_t *rec,
                        const char *text)
{
    uint16_t id, old, len = 0;

    while (text[len])
        ++len;
    memcpy(&old, rec->val, 2);
    if (strpool_add(text, len, &id) != ERR_OK)
        return 0;

    rec->val[0] = (uint8_t)id;
    rec->val[1] = (uint8_t)(id >> 8);
    cells_set((cellstore_t *)wb_cells(), r, c, rec);
    strpool_unref(old);
    wb_dirty = 1;
    return 1;
}

static uint8_t replace_run(void)
{
    const cellstore_t *cs = wb_cells();
    uint16_t r, c;
    uint8_t all = 0, done = 0, y = (uint8_t)(screen_rows() - 1);


    if (!find_ask() || !ask_text(S_repp, (uint8_t)(sizeof S_repp - 1),
                                 rep_text, sizeof rep_text, 1)
        || cs->cell_count == 0) {
        unask();
        return 0;
    }

    for (r = 0; r <= cs->max_row; ++r)
        for (c = 0; c <= cs->max_col; ++c) {
            cell_record_t rec;

            /* ANY cell but a formula. It was text-only, which meant
             * searching a sheet of numbers for "2" found nothing and
             * looked broken -- and a number is exactly what people replace.
             * The substitution runs on the edit text and the result goes
             * back through put_value(), so 222 -> 999 stays a number and
             * 222 -> 9x9 becomes a label, which is what typing it would
             * have done.
             *
             * A FORMULA IS STILL SKIPPED: rewriting its source needs the
             * compiler, which is another overlay, and a careless search
             * through one destroys it. */
            if (!cells_get(cs, r, c, &rec) || rec.type == CELL_FORMULA)
                continue;
            wb_edit_text(r, c, rep_in, sizeof rep_in);
            if (!contains(rep_in, find_text) || !rep_build())
                continue;

            if (!all) {
                uint8_t k;

                grid_goto(r, c);                /* show which one */
                screen_fill(0, y, screen_cols(), ' ', C_ASK);
                screen_text(0, y, S_repq, (uint8_t)(sizeof S_repq - 1),
                            C_ASK);
                k = (uint8_t)up((char)kbd_wait());
                if (k == '\x1B')
                    goto done;
                if (k == 'A')
                    all = 1;
                else if (k != 'Y')
                    continue;
            }
            /* The cell's own style, not 0: replacing the text inside a
             * currency cell must not turn it into a bare number. */
            done |= put_value(r, c, rep_out, rec.style);
        }

done:
    unask();
    return done;
}

/* --- copy and paste ----------------------------------------------------
 *
 * HERE rather than in the grid, and that move paid for the rest of this:
 * the old clipboard was 256 bytes of golden RAM and its two functions were
 * resident, and none of it is now. What stayed behind is two more case
 * labels on a group that already existed.
 *
 * The clipboard holds TEXT -- what the formula bar would show -- rather
 * than copies of the cell records, so pasting is wb_set_text()'s existing
 * job of parsing a number, recognising TRUE/FALSE and compiling a formula.
 * Copying records would mean owning a banked formula record and a pool
 * reference for as long as the clipboard held them, with all the release
 * rules that go with that.
 *
 * A formula is stored as its SOURCE, so it pastes as a formula.
 *
 * THE FORMAT DOES NOT TRAVEL: copying a currency cell and pasting it gives
 * the right number without the currency format.
 */
static const char S_toobig[] = "Too long to copy";
static const char S_copied[] = "Copied";

/* The longest cell text a copy will carry.
 *
 * MUST STAY AT OR BELOW SHIFT_MAX in fref.c, which is what the clipboard is
 * read into on the way out. Sized by what fits in this overlay rather than
 * by WB_TEXT_MAX; 56 is about twice the longest formula in any of the
 * example workbooks.
 *
 * A cell whose text does not fit REFUSES THE COPY rather than truncating,
 * because a truncated formula pasted back is a destroyed one. Lives in
 * OVL14BSS, not on the C stack. */
#define CLIP_MAX 56
static char clip_buf[CLIP_MAX + 1];

static void note(const char *s, uint8_t n)
{
    uint8_t y = (uint8_t)(screen_rows() - 1);

    screen_fill(0, y, screen_cols(), ' ', C_ASK);
    screen_text(1, y, s, n, C_ASK);
}

static uint8_t clip_ok(void)
{
    return (uint8_t)(clip_rows != 0 && clip_h != H_NULL);
}

/* Measure one cell's text.
 *
 * BOTH PASSES OF A COPY MUST USE THIS. One pass decides how many bytes to
 * allocate and the other how many to write, and bank_write() does no bounds
 * checking -- so if the two ever measure a cell differently, the copy
 * overruns its block and corrupts the allocator's bookkeeping. */
#define CLIP_READ ((uint8_t)(sizeof clip_buf - 1))

static uint16_t cell_len(uint16_t r, uint16_t c)
{
    wb_edit_text(r, c, clip_buf, CLIP_READ);
    return (uint16_t)strlen(clip_buf);
}

static uint16_t clip_size(void)
{
    uint16_t r, c, n = 0;

    for (r = sel_r1; r <= sel_r2; ++r)
        for (c = sel_c1; c <= sel_c2; ++c) {
            uint16_t len = cell_len(r, c);

            /* Filled the buffer means it was cut short, and a truncated
             * formula pasted back is a destroyed one. */
            if (len + 2 > CLIP_READ)
                return 0;
            n = (uint16_t)(n + len + 2);        /* text, NUL, style */
            if (n > BANK_MAX_ALLOC)
                return 0;               /* refuse rather than truncate */
        }
    return n;
}

static uint8_t copy_run(void)
{
    uint16_t r, c, off = 0, need = clip_size();
    handle_t h;

    if (need == 0) {
        note(S_toobig, (uint8_t)(sizeof S_toobig - 1));
        return 0;                       /* the old clipboard survives */
    }

    /* Release the old block BEFORE taking a new one, and only while
     * clip_rows still vouches for it.
     *
     * The other order looks safer and is not: the allocator handed back
     * the very address the old handle held -- because the heap had been
     * rebuilt underneath it -- and the free that came afterwards then
     * released the block just filled. A copy read back empty and only the
     * second paste of a test noticed. */
    if (clip_rows)
        bank_free(clip_h);
    clip_rows = 0;

    h = bank_alloc(need);
    if (h == H_NULL) {
        note(S_toobig, (uint8_t)(sizeof S_toobig - 1));
        return 0;
    }

    for (r = sel_r1; r <= sel_r2; ++r)
        for (c = sel_c1; c <= sel_c2; ++c) {
            uint16_t n = (uint16_t)(cell_len(r, c) + 1);
            cell_record_t rec;

            /* Cannot fire now that both passes measure the same way -- and
             * it stays because bank_write() would not have told us. */
            if ((uint16_t)(off + n + 1) > need)
                break;
            bank_write(h, off, clip_buf, n);
            off = (uint16_t)(off + n);

            /* The style travels beside the text, one byte after its NUL, so
             * a currency cell pastes as currency rather than as a bare
             * number. Both readers of this block step over it -- see
             * clip_at() here and in fref.c. */
            bank_poke(h, off, wb_get(r, c, &rec) ? rec.style : 0);
            ++off;
        }

    clip_h    = h;
    clip_rows = (uint16_t)(sel_r2 - sel_r1 + 1);
    clip_cols = (uint16_t)(sel_c2 - sel_c1 + 1);
    clip_row0 = sel_r1;
    clip_col0 = sel_c1;

    /* Confirm it, or a copy that refused, one that succeeded and one that
     * never ran all look identical. The word alone -- the block's size
     * would be more useful but does not fit in this overlay. */
    note(S_copied, (uint8_t)(sizeof S_copied - 1));
    return 0;                           /* nothing in the sheet changed */
}

/* Read the `i`th entry of the clipboard into `out`, and say where the next
 * one starts. The block is NUL-terminated strings back to back, so this
 * walks -- there is no index, because an index would cost more than the
 * walk on a block this size. */
static uint16_t clip_at(uint16_t off, char *out, uint16_t max)
{
    uint16_t n = 0;

    do {
        out[n] = (char)bank_peek(clip_h, (uint16_t)(off + n));
    } while (out[n] && ++n < (uint16_t)(max - 1));
    out[n] = 0;
    return (uint16_t)(off + n + 1);
}

static uint8_t paste_run(void)
{
    uint16_t r, c, off = 0;
    uint8_t was;

    if (!clip_ok())
        return 0;

    /* One recalculation for the block, not one a cell: every wb_set_text()
     * ends in wb_after_change(). The same clamp the block clear uses. */
    was = wb_manual;
    wb_manual = 1;

    for (r = 0; r < clip_rows; ++r)
        for (c = 0; c < clip_cols; ++c) {
            uint16_t dr = (uint16_t)(grid_state()->cur_row + r);
            uint16_t dc = (uint16_t)(grid_state()->cur_col + c);

            uint8_t sty;

            off = clip_at(off, clip_buf, sizeof clip_buf);
            sty = bank_peek(clip_h, off);
            ++off;                      /* the style byte */
            if (dr >= X16S_MAX_ROWS || dc >= X16S_MAX_COLS)
                continue;               /* clipped at the edge */
            /* A formula is left to OVL_FCOMPILE; the grid loads it after
             * this returns. Everything else lands now. */
            put_value(dr, dc, clip_buf, sty);
        }

    wb_manual = was;

    /* 1 even when every cell of the block was a formula and none of them
     * landed here: the grid reads this as "go on", and going on is what
     * loads the compiler for them. Returning "nothing changed" skipped the
     * whole second half. */
    return 1;
}

/* Turn bold on or off over the selection.
 *
 * One command, not two: whether the block is being made bold or plain is
 * decided by the FIRST cell in it that exists. Bold already, and the block
 * comes off; otherwise it goes on. That is what makes a single menu entry
 * behave the way a toolbar button does.
 *
 * Styles are interned, so this does not edit one in place -- editing the
 * style a cell names would change every other cell naming it too. It asks
 * for the style it wants and takes back whatever id that turns out to be,
 * new or already there.
 *
 * With no block, sel_r1..sel_c2 collapse onto the cursor, so this is one
 * cell without needing to know that.
 */
static uint8_t bold_run(void)
{
    cellstore_t *cs = (cellstore_t *)wb_cells();
    cell_record_t rec;
    cell_style_t st;
    uint16_t r, c;
    uint8_t on = 0, id, seen = 0;

    for (r = sel_r1; r <= sel_r2 && !seen; ++r)
        for (c = sel_c1; c <= sel_c2; ++c)
            if (cells_get(cs, r, c, &rec)) {
                styles_get(rec.style, &st);
                on = (uint8_t)!(st.flags & STY_BOLD);
                seen = 1;
                break;
            }
    if (!seen)
        return 0;                       /* an empty block: nothing to mark */

    for (r = sel_r1; r <= sel_r2; ++r)
        for (c = sel_c1; c <= sel_c2; ++c) {
            if (!cells_get(cs, r, c, &rec))
                continue;
            styles_get(rec.style, &st);
            if (on)
                st.flags = (uint8_t)(st.flags | STY_BOLD);
            else
                st.flags = (uint8_t)(st.flags & ~STY_BOLD);
            if (styles_add(&st, &id) != ERR_OK)
                return 1;               /* the table is full; keep what took */
            rec.style = id;
            rec.col = (uint8_t)c;
            cells_set(cs, r, c, &rec);
        }

    wb_dirty = 1;
    return 1;
}

uint8_t menu_cmd(uint8_t key)
{
    /* HERE, for every command, not in each one that happens to remember.
     *
     * The grid checks row_shift_by after this returns and, if it is set,
     * shifts every reference in the workbook. Only row_cmd() ever means
     * it. A paste that ran after an insert row inherited the old value and
     * moved every formula in the sheet -- a test caught it, which is the
     * only reason it is not in the machine. */
    row_shift_by = 0;

    if (key == MENU_INS_ROW || key == MENU_DEL_ROW)
        return row_cmd((uint8_t)(key == MENU_DEL_ROW));
    if (key == MENU_BOLD)
        return bold_run();
    if (key == MENU_COPY)
        return copy_run();
    if (key == MENU_PASTE)
        return paste_run();
    if (key == MENU_COL_W)
        col_width_run();        /* these two repaint the grid themselves */
    else if (key == MENU_REPLACE)
        return replace_run();       /* the grid recalculates and repaints */
    else
        find_run();
    return 0;
}

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
