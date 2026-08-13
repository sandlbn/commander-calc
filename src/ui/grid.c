#include "grid.h"
#include "editor.h"
#include "filedlg.h"
#include "menu.h"
#include "../workbook/workbook.h"
#include "../workbook/native_file.h"
#include "../formula/formula.h"
#include "chart.h"
#include "../import/csv.h"
/* Import and Export handle both .xlsx and CSV: is_xlsx() picks by
 * extension. X16S_XLSX_UI is on in X16FLAGS and has been since the resident
 * driver was made to fit; the flag stays so the host build can leave the
 * .xlsx paths out when it wants to. */
#ifdef X16S_XLSX_UI
#  include "../import/xlsx_import.h"
#  include "../export/xlsx_export.h"

#endif
#include "../platform/overlay.h"
#include "../platform/banked_ram.h"
#include "../platform/file_io.h"
#include "../platform/mouse.h"
#include <string.h>

/* --- key codes, from x16-docs chapter 03 ------------------------- */
#define K_PGDN      0x02
#define K_END       0x04
#define K_TAB       0x09
#define K_RETURN    0x0D
#define K_DOWN      0x11
#define K_HOME      0x13
#define K_SHIFTTAB  0x18
#define K_ESC       0x1B
#define K_RIGHT     0x1D
#define K_PGUP      0x82
#define K_DEL       0x14        /* backspace: clear the cell */
#define K_FWDDEL    0x19
#define K_F1        0x85        /* new                         */
#define K_F2        0x89        /* edit the current value      */
#define K_F3        0x86        /* open                        */
#define K_F4        0x8A        /* save                        */
#define K_F5        0x87        /* undo                        */
#define K_F6        0x8B        /* save as                     */
#define K_F7        0x88        /* import CSV or XLSX          */
#define K_F8        0x8C        /* export CSV or XLSX          */
#define K_F9        0x10        /* quit                        */
#define K_F10       0x15        /* the menu bar                */
/* Copy and paste.
 *
 * $03 and $16 are what Ctrl+C and Ctrl+V produce, and neither is a code
 * this program already answers to -- $16 is also F11, which is fine
 * because F11 does nothing else here. F12 ($17) was the other candidate
 * and is not used: emulators take it for their debugger.
 *
 * Both are on the Edit menu as well, which is the path that cannot be
 * wrong about what the keyboard sends. */
#define K_SELECT    0x01        /* Ctrl+A: anchor a block, or drop it */
#define K_COPY      0x03
#define K_PASTE     0x16
#define K_FIND      0x06        /* Ctrl+F, and on the Edit menu */
#define K_COL_W     0x9E        /* Layout > Column width; see menu.h */
#define K_REPLACE   0x9F        /* Edit > Replace */
#define K_INS_COL   0x1E        /* Edit > Insert/Delete column; see menu.h */
#define K_DEL_COL   0x1F
/* Sheet commands.
 *
 * These are not codes the keyboard produces. The menu returns the code of
 * whatever was chosen and grid_key() dispatches it exactly as if it were a
 * key, which is what keeps one implementation per command — so they need
 * values no real key uses. $F0 upwards is clear: the X16 sends nothing
 * above $9D. Kept in step with the same names in menu.c. */
#define K_SHEET_NEXT 0xF0
#define K_SHEET_PREV 0xF1
#define K_SHEET_ADD  0xF2
#define K_SHEET_DEL  0xF3
#define K_SHEET_REN  0xF4
#define K_SORT_ASC   0xF5
#define K_SORT_DESC  0xF6
#define K_SORT_UNDO  0xF7
#define K_INS_ROW    0xF8
#define K_DEL_ROW    0xF9
#define K_CALC_NOW   0xFA
#define K_CALC_AUTO  0xFB
#define K_FREEZE     0xFC

/* The three chart commands, consecutive and in CHART_* order, so the type
 * is the difference and one test catches all three. Highest codes in the
 * program, which is what lets the test be `>=`. */
#define K_CHART      0xFD
#define K_UP        0x91
#define K_CLEAR     0x93        /* shift+home */
#define K_LEFT      0x9D

/* --- palette ----------------------------------------------------- *
 *
 * The look of a DOS spreadsheet: black paper, white figures, a light grey
 * frame around them, and a cursor in reverse video. Saturated, not pastel
 * -- on a CRT the strongest thing on screen should be the numbers.
 *
 * Two earlier attempts are worth naming so they are not tried again.
 * Banding alternate blocks of rows fought with the columns. A pale scheme
 * -- grey paper, green frame, cyan bars -- was legible but washed out,
 * because the X16 palette's mid tones are all close together and nothing
 * stood out from anything else.
 *
 * None of this costs a byte: an attribute byte is an attribute byte, and
 * the same screen_text call draws whichever one it is given. */
#define C_MENU    COLOR(COL_BLACK, COL_LGREY)   /* the frame, top       */
#define C_FORMULA COLOR(COL_YELLOW, COL_BLACK)  /* what is in the cell  */
#define C_HEADER  COLOR(COL_BLACK, COL_LGREY)   /* row and column labels */
#define C_HDR_SEL COLOR(COL_BLACK, COL_YELLOW)  /* ...where the cursor is */
/* White paper, black figures -- Excel 3 rather than a terminal. The rest
 * of the scheme is unchanged: a grey frame, yellow where the cursor's row
 * and column meet the headers, and the formula bar in yellow on black so
 * that the line you type on is not the same surface you read from.
 *
 * Only these two moved, and deliberately so. Recolouring the whole palette
 * to match cost seven resident bytes -- swapping which constants happen to
 * share an attribute value is enough to change what cc65 emits -- and
 * there were five. */
#define C_CELL    COLOR(COL_BLACK, COL_WHITE)   /* the paper            */
#define C_CURSOR  COLOR(COL_WHITE, COL_BLACK)   /* reverse video        */
#define C_SEL     COLOR(COL_BLACK, COL_LGREY)   /* a selected block     */
#define C_TABS    COLOR(COL_BLACK, COL_LGREY)
#define TAB_W     10
#define C_STATUS  COLOR(COL_BLACK, COL_LGREY)   /* the frame, bottom    */
#define C_EDIT    COLOR(COL_YELLOW, COL_BLACK)  /* typing shows yellow  */
#define C_CARET   COLOR(COL_BLACK, COL_YELLOW)

/* The closed menu bar, with the key that opens it. menu.c has the open
 * version and the two must list the same menus.
 *
 * This replaced a whole row of F-key abbreviations at the bottom of the
 * screen. The menu names the same commands in full and shows their keys
 * beside them, so the legend was saying it twice — badly, since it had to
 * abbreviate to fit. */
static const char S_bar[]          = " File  Edit  Layout  Data  Chart  Sheet";

/* The program has never said what it is called anywhere. Right-aligned on
 * the menu bar, which is the one row that is always drawn and never
 * scrolls. Resident, like S_bar and for the same reason. */
static const char S_name[]         = "Commander Calc";

/* Not static: read directly through the grid_state() macro. A function
 * that returns the address of a fixed object is a call for nothing, and
 * three overlays make it. See grid.h. */
/* In the ZERO PAGE, not golden RAM, and that is worth 112 references in
 * this file alone -- every one of them a byte shorter, and two bytes for
 * the 16-bit fields. See X16S_ZP_BEGIN in x16sheet.h. */
X16S_ZP_BEGIN
grid_t grid;
X16S_ZP_END

/* See menu.h: the menu bar's own overlay cannot hold this. */
X16S_GOLDEN_BEGIN
uint8_t menu_open_x;

/* What Find last looked for, so that running it again offers the same
 * text and walks to the next match. See find_run(). */
X16S_GOLDEN_BEGIN
char find_text[16];

/* And what Replace last put in, remembered the same way -- coming back to
 * the prompt with the last replacement already in it is the point of a
 * prompt that starts from what is there.
 *
 * HERE, not in menu.c where the rest of Replace is. AN OVERLAY'S BSS IS
 * NEVER ZEROED (see OVL_CODE_BEGIN in x16sheet.h), so in OVL14 this began
 * as whatever the last overlay left in those sixteen bytes, and the prompt
 * opened with a line of garbage that had never been typed. Golden RAM is
 * cleared by golden_init(), which is exactly the difference.
 *
 * The host suite cannot catch this: with no overlays a static is zeroed,
 * so it passes there and fails on the machine. */
char rep_text[16];
X16S_GOLDEN_END

/* The last row inserted or deleted. See menu.h. */
/* Zero page, which is not part of the BSS area at all -- so these three
 * bytes are three bytes of the resident image given back, on top of being
 * a byte shorter at every reference. */
X16S_ZP_BEGIN
uint16_t row_shift_at;
int8_t   row_shift_by;
X16S_ZP_END

/* The clipboard: a banked block of the copied cells' text, and where it
 * came from. Resident for the same reason the sort's permutation is --
 * OVL_MENU fills it and OVL_FCOMPILE reads it, and an overlay's own bss
 * does not survive the swap between. Guarded by clip_rows, never by the
 * handle: see after_file_op(). */
handle_t clip_h;
uint16_t clip_rows, clip_cols;
uint16_t clip_row0, clip_col0;

/* And the last sort, so it can be undone. See menu.h. */
handle_t sort_undo_idx;
uint16_t sort_undo_n;
uint8_t  sort_undo_sheet;
uint16_t sort_undo_base;
uint16_t sort_undo_c0, sort_undo_c1;
/* The used range as it was when the sort ran. */
uint16_t sort_undo_maxrow;
/* The block the next sort will move; see sort_range(). */
uint16_t sort_r0, sort_r1, sort_c0, sort_c1, sort_keycol;
X16S_GOLDEN_END
static const char *status_text = "";

/* The workbook's filename, empty until it has been saved or opened once.
 * Golden RAM: it is read as a string, so it does need to start out empty,
 * which golden_init() guarantees. */
X16S_GOLDEN_BEGIN
static char cur_name[FILE_NAME_MAX];
X16S_GOLDEN_END

/* Row indices derived from the screen size, so a 40-column or 80x30 mode
 * still lays out correctly. */
#define ROW_MENU     0
#define ROW_FORMULA  1
#define ROW_COLHDR   2
#define ROW_GRID0    3
#define ROW_TABS     ((uint8_t)(screen_rows() - 3))
#define ROW_STATUS   ((uint8_t)(screen_rows() - 2))

static uint8_t col_w(uint16_t c)
{
    return wb_col_width(c);
}

/* --- names -------------------------------------------------------- */

void grid_col_name(uint16_t col, char *out)
{
    char tmp[4];
    uint8_t n = 0;
    uint16_t c = col;

    /* Bijective base 26: after the first letter there is no zero digit, so
     * each further place is (c / 26 - 1). That is what makes column 26 "AA"
     * rather than "BA". */
    for (;;) {
        tmp[n++] = (char)('A' + (c % 26));
        if (c < 26)
            break;
        c = c / 26 - 1;
    }
    while (n)
        *out++ = tmp[--n];
    *out = '\0';
}

static void u16_str(uint16_t v, char *out)
{
    char tmp[6];
    uint8_t n = 0;

    if (v == 0) {
        *out++ = '0';
        *out = '\0';
        return;
    }
    while (v) {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (n)
        *out++ = tmp[--n];
    *out = '\0';
}

void grid_cell_name(uint16_t row, uint16_t col, char *out)
{
    grid_col_name(col, out);
    while (*out)
        ++out;
    u16_str((uint16_t)(row + 1), out);      /* rows are 1-based on screen */
}

/* --- geometry -----------------------------------------------------
 *
 * With frozen headings the screen is no longer a window onto a rectangle:
 * the first frz_row rows and frz_col columns are always rows 0.. and
 * columns 0.., and the rest scrolls behind them. Everything that turns a
 * screen position into a sheet position goes through these two, and
 * nothing else does the arithmetic.
 */

/* Sheet row shown on screen row `sy` (0 = the first data row). */
static uint16_t row_at(uint8_t sy)
{
    return sy < grid.frz_row
         ? sy
         : (uint16_t)(grid.top_row + sy - grid.frz_row);
}

/* Sheet column in the `i`th column position across. */
static uint16_t col_nth(uint16_t i)
{
    return i < grid.frz_col
         ? i
         : (uint16_t)(grid.left_col + i - grid.frz_col);
}

/* Screen row showing sheet row `row`. The inverse of row_at().
 *
 * ONLY VALID FOR A ROW THAT IS ON SCREEN. It does not check, and it is not
 * asked to: its one caller is grid_goto()'s fast path, which runs only when
 * the viewport did not move, and both the row the cursor left and the row
 * it arrived at are visible by construction. Bounds-checking there cost
 * more than the whole freezing feature had left to spend. */
static uint8_t row_sy(uint16_t row)
{
    return row < grid.frz_row
         ? (uint8_t)row
         : (uint8_t)(row - grid.top_row + grid.frz_row);
}

int16_t grid_col_x(uint16_t col)
{
    uint16_t i, c;
    uint16_t x = GRID_ROWHDR_W;

    /* Scrolled off to the left. A frozen column never is. */
    if (col >= grid.frz_col && col < grid.left_col)
        return -1;

    for (i = 0; (c = col_nth(i)) != col; ++i) {
        x += col_w(c);
        if (x >= screen_cols())
            return -1;
    }
    return x < screen_cols() ? (int16_t)x : -1;
}

/* How much of a column is actually on screen: the last one is clipped
 * rather than hidden, which is what makes the right edge look continuous. */
/* How much of a column at screen x is drawable: all of it, or what is left
 * to the right edge. */
static uint8_t clip_w(uint8_t x, uint8_t cw)
{
    return (x + cw > screen_cols()) ? (uint8_t)(screen_cols() - x) : cw;
}

static uint8_t col_visible_w(uint16_t col)
{
    int16_t x = grid_col_x(col);

    if (x < 0)
        return 0;
    if ((uint16_t)x + col_w(col) > screen_cols())
        return (uint8_t)(screen_cols() - (uint16_t)x);
    return col_w(col);
}

/* Would freezing at the cursor leave a column to scroll?
 *
 * Frozen columns are drawn before anything else, so wide ones can fill the
 * screen on their own. Measured rather than capped at some round number:
 * column widths come out of the imported file and four of them can be
 * anything from 12 characters to the whole row. */
/* Would freezing `upto` columns still leave room for a scrolling one? */
static uint8_t freeze_cols_fit(uint16_t upto)
{
    uint16_t c, x = GRID_ROWHDR_W;

    for (c = 0; c < upto; ++c)
        x += col_w(c);
    return x + GRID_DEF_COL_W <= screen_cols();
}

static void ensure_visible(void)
{
    /* A cursor sitting in the frozen part is always visible and must not
     * drag the scrolling part with it. */
    uint8_t vis = (uint8_t)(grid.grid_rows - grid.frz_row);

    if (grid.cur_row >= grid.frz_row) {
        if (grid.cur_row < grid.top_row)
            grid.top_row = grid.cur_row;
        else if (grid.cur_row >= grid.top_row + vis)
            grid.top_row = (uint16_t)(grid.cur_row - vis + 1);
        if (grid.top_row < grid.frz_row)
            grid.top_row = grid.frz_row;
    }

    if (grid.cur_col < grid.frz_col)
        return;
    if (grid.cur_col < grid.left_col) {
        grid.left_col = grid.cur_col;
        if (grid.left_col < grid.frz_col)
            grid.left_col = grid.frz_col;
        return;
    }
    for (;;) {
        int16_t x = grid_col_x(grid.cur_col);
        if (x >= 0 && (uint16_t)x + col_w(grid.cur_col) <= screen_cols())
            return;
        if (grid.left_col >= grid.cur_col)
            return;              /* column wider than the screen: clip it */
        ++grid.left_col;
    }
}

/* --- painting ----------------------------------------------------- */

/* Drawn on every render, so it has to be here: loading OVL_MENU to paint
 * one row would be absurd. See S_bar above. */
static void render_menu(void)
{
    screen_text(0, ROW_MENU, S_bar, screen_cols(), C_MENU);
    screen_text((uint8_t)(screen_cols() - sizeof S_name), ROW_MENU, S_name,
                (uint8_t)(sizeof S_name - 1), C_MENU);
}

#define FBAR_X ((uint8_t)(GRID_ROWHDR_W + 6))

static void render_formula_bar(void)
{
    char name[9];
    char text[WB_TEXT_MAX];
    uint8_t w = (uint8_t)(screen_cols() - FBAR_X);
    color_t c = editor_active() ? C_EDIT : C_FORMULA;

    grid_cell_name(grid.cur_row, grid.cur_col, name);
    screen_text(0, ROW_FORMULA, " ", 1, c);
    screen_text(1, ROW_FORMULA, name, GRID_ROWHDR_W + 3, c);
    screen_put((uint8_t)(GRID_ROWHDR_W + 4), ROW_FORMULA, '|', c);

    if (editor_active()) {
        const char *b = editor_text();
        uint8_t caret = editor_caret();
        /* Scroll the visible window so the caret stays on screen even when
         * the text is longer than the bar. */
        uint8_t from = caret >= w ? (uint8_t)(caret - w + 1) : 0;
        screen_text(FBAR_X, ROW_FORMULA, b + from, w, c);
        screen_recolor((uint8_t)(FBAR_X + caret - from), ROW_FORMULA, 1,
                       C_CARET);
    } else {
        wb_edit_text(grid.cur_row, grid.cur_col, text, sizeof text);
        screen_text(FBAR_X, ROW_FORMULA, text, w, c);
    }
}

static void render_colhdr(void)
{
    char name[4];
    uint16_t c, i;
    uint8_t  x;

    screen_fill(0, ROW_COLHDR, screen_cols(), ' ', C_HEADER);
    /* Drawn once per scroll rather than once per row, so this keeps the
     * straightforward form even though grid_col_x() re-walks the columns
     * each time. render_row() is where that mattered. */
    for (i = 0; (c = col_nth(i)) < X16S_MAX_COLS; ++i) {
        uint8_t w = col_visible_w(c);
        if (!w)
            break;
        x = (uint8_t)grid_col_x(c);
        grid_col_name(c, name);
        /* Centre-ish: one leading space reads as a column separator. */
        screen_text(x, ROW_COLHDR, " ", 1,
                    c == grid.cur_col ? C_HDR_SEL : C_HEADER);
        if (w > 1)
            screen_text((uint8_t)(x + 1), ROW_COLHDR, name, (uint8_t)(w - 1),
                        c == grid.cur_col ? C_HDR_SEL : C_HEADER);
    }
}

/* Draw one cell's value in its column. Values are clipped to the column
 * rather than spilling into the neighbour: overflow into adjacent empty
 * cells is a step-15 refinement, and a value that silently overwrote real
 * data next door would be worse than one that is visibly cut off. */
/* The selection, sorted, recomputed when the anchor or cursor moves rather
 * than per cell -- in_sel() runs for every cell of every render.
 *
 * ALWAYS VALID: with no block they collapse onto the cursor, which lets a
 * command act on "the selection" without asking whether there is one. */
/* Golden RAM rather than zero page. OVL_MENU reads all four when it copies
 * a block, and cross-module reads of a zero-page global are the one thing
 * ld65 cannot type correctly; ordinary memory keeps four warnings out of
 * the link for eight bytes of an area with room to spare. */
X16S_GOLDEN_BEGIN
uint16_t sel_r1, sel_c1, sel_r2, sel_c2;
X16S_GOLDEN_END

static void sel_bounds(void)
{
    uint16_t a = grid.sel_on ? grid.sel_row : grid.cur_row;
    uint16_t b = grid.cur_row;

    sel_r1 = a < b ? a : b;
    sel_r2 = a < b ? b : a;

    a = grid.sel_on ? grid.sel_col : grid.cur_col;
    b = grid.cur_col;
    sel_c1 = a < b ? a : b;
    sel_c2 = a < b ? b : a;
}

#define in_sel(row, col) (grid.sel_on \
    && (row) >= sel_r1 && (row) <= sel_r2 \
    && (col) >= sel_c1 && (col) <= sel_c2)

#ifndef __CC65__
/* Host only. The machine's callers -- this file and OVL_MENU -- read the
 * four directly; only the tests want them through a call. */
void grid_sel(uint16_t *r1, uint16_t *c1, uint16_t *r2, uint16_t *c2)
{
    *r1 = sel_r1; *c1 = sel_c1; *r2 = sel_r2; *c2 = sel_c2;
}
#endif

static void render_cell_at(uint8_t x, uint8_t y, uint8_t w,
                           uint16_t row, uint16_t col)
{
    char text[WB_TEXT_MAX];
    cell_record_t rec;
    color_t c = (row == grid.cur_row && col == grid.cur_col)
                    ? C_CURSOR : (in_sel(row, col) ? C_SEL : C_CELL);

    if (!wb_get(row, col, &rec)) {
        screen_text(x, y, "", w, c);
        return;
    }

    wb_display_text(row, col, text, sizeof text);
    if (wb_align_right(&rec))
        screen_text_right(x, y, text, w, c);
    else
        screen_text(x, y, text, w, c);
}

/* One data row: its header, then every visible cell. */
static void render_row(uint8_t sy)
{
    uint16_t row = row_at(sy);
    char label[6];
    uint16_t c, i;
    uint8_t  x;             /* 80 columns fit in a byte; a wider type here
                             * costs 16-bit arithmetic on every column */

    if (row >= X16S_MAX_ROWS) {
        screen_fill(0, (uint8_t)(ROW_GRID0 + sy), screen_cols(), ' ', C_CELL);
        return;
    }

    u16_str((uint16_t)(row + 1), label);
    screen_text_right(0, (uint8_t)(ROW_GRID0 + sy), label, GRID_ROWLBL_W,
                      row == grid.cur_row ? C_HDR_SEL : C_HEADER);
    screen_put(GRID_ROWLBL_W, (uint8_t)(ROW_GRID0 + sy), ' ',
               row == grid.cur_row ? C_HDR_SEL : C_HEADER);

    /* Same accumulation as the header, and for the same reason. */
    x = GRID_ROWHDR_W;
    for (i = 0; (c = col_nth(i)) < X16S_MAX_COLS && x < screen_cols(); ++i) {
        uint8_t cw = col_w(c);

        render_cell_at(x, (uint8_t)(ROW_GRID0 + sy), clip_w(x, cw), row, c);
        x = (uint8_t)(x + cw);      /* < 80 + 40, so no wrap */
    }
}

static void render_grid(void)
{
    uint8_t sy;

    for (sy = 0; sy < grid.grid_rows; ++sy)
        render_row(sy);
}

/* One tab per sheet, the active one inverted, truncated at the right edge
 * rather than wrapped. The filename follows if there is room for it. */
static void render_tabs(void)
{
    char name[WB_TAB_NAME];
    uint8_t i, x = 0;

    screen_fill(0, ROW_TABS, screen_cols(), ' ', C_TABS);
    /* Fixed-width tabs. Measuring each name to size its tab reads better on
     * screen and cost 150 resident bytes to do it; a long name is truncated
     * instead, which is what the column headers do too. */
    for (i = 0; i < wb_sheet_n && x + TAB_W <= screen_cols(); ++i) {
        wb_tab_name(i, name);
        screen_text((uint8_t)(x + 1), ROW_TABS, name, (uint8_t)(TAB_W - 1),
                    i == wb_sheet_i ? C_HDR_SEL : C_TABS);
        x = (uint8_t)(x + TAB_W);
    }
    if (cur_name[0] && x + 2 < screen_cols())
        screen_text((uint8_t)(x + 2), ROW_TABS, cur_name,
                    (uint8_t)(screen_cols() - x - 2), C_TABS);
}

static const char S_recalc[] = "CALC";
/* Freeze keeps what is above and left of the cursor, and refuses when the
 * frozen part would leave nothing to scroll. */
static const char S_nofrz[] = "Nothing to freeze here";
/* Patched in place rather than painted a character at a time -- four
 * screen_put calls cost more resident bytes than this whole feature is
 * allowed to spend, and four stores into a string cost almost nothing. */
static char S_noovl[] = "No ovl: x";

static void render_status(void)
{
    char name[9];

    screen_fill(0, ROW_STATUS, screen_cols(), ' ', C_STATUS);
    screen_text(1, ROW_STATUS,
                editor_active() ? "EDIT " : (wb_dirty ? "MODIF" : "READY"),
                5, C_STATUS);
    grid_cell_name(grid.cur_row, grid.cur_col, name);
    screen_text(8, ROW_STATUS, name, 8, C_STATUS);
    screen_text(18, ROW_STATUS, status_text,
                (uint8_t)(screen_cols() - 19), C_STATUS);

    /* Right-hand end, where Lotus put it, and only when something really
     * is stale. Manual recalculation with nothing outstanding says nothing:
     * the state shows up on the very next edit anyway. */
    if (wb_calc_due)
        screen_text((uint8_t)(screen_cols() - 5), ROW_STATUS, S_recalc, 4,
                    C_STATUS);

}


void grid_render_all(void)
{
    screen_clear(C_CELL);
    render_menu();
    render_formula_bar();
    render_colhdr();
    render_grid();
    render_tabs();
    render_status();
}

/* Repaint just the cell at (row,col), if it happens to be on screen. The
 * cursor colour follows from where the cursor is, so this needs no colour
 * argument — and redrawing the text as well as the attribute means an edit
 * shows up without touching the rest of the row. */

static void repaint_cell(uint16_t row, uint16_t col)
{
    int16_t x = grid_col_x(col);
    uint8_t w;

    if (x < 0 || row < grid.top_row || row >= grid.top_row + grid.grid_rows)
        return;
    w = col_visible_w(col);
    if (!w)
        return;
    render_cell_at((uint8_t)x, (uint8_t)(ROW_GRID0 + (row - grid.top_row)),
                   w, row, col);
}

/* --- movement ----------------------------------------------------- */

void grid_goto(uint16_t row, uint16_t col)
{
    uint16_t old_row = grid.cur_row, old_col = grid.cur_col;
    uint16_t old_top = grid.top_row, old_left = grid.left_col;

    if (row >= X16S_MAX_ROWS)
        row = X16S_MAX_ROWS - 1;
    if (col >= X16S_MAX_COLS)
        col = X16S_MAX_COLS - 1;

    grid.cur_row = row;
    grid.cur_col = col;
    sel_bounds();
    ensure_visible();

    /* A block makes every move a full repaint: the selection grew or
     * shrank, so far more than two cells changed. Folded into the
     * viewport test rather than given a branch of its own. */
    if (grid.sel_on || grid.top_row != old_top
        || grid.left_col != old_left) {
        /* The viewport moved, so everything below the headers is stale. */
        render_colhdr();
        render_grid();
    } else if (row != old_row || col != old_col) {
        /* It did not: two recolours and the headers are the whole update.
         * Repainting the grid here would make every arrow key visibly
         * redraw 54 rows. */
        repaint_cell(old_row, old_col);
        repaint_cell(row, col);
        render_colhdr();
        if (row != old_row) {
            render_row(row_sy(old_row));
            render_row(row_sy(row));
        }
    }
    render_formula_bar();
    render_status();
}

/* Saturating add of a signed delta to an unsigned position. Done in 16 bits
 * because a 32-bit intermediate costs several times as much on a 6502, and
 * this runs on every arrow key. */
static uint16_t step(uint16_t pos, int16_t delta, uint16_t limit)
{
    uint16_t d;

    if (delta < 0) {
        d = (uint16_t)(-delta);
        return d > pos ? 0 : (uint16_t)(pos - d);
    }
    d = (uint16_t)delta;
    return (uint16_t)(limit - pos) < d ? limit : (uint16_t)(pos + d);
}

void grid_move(int16_t dr, int16_t dc)
{
    grid_goto(step(grid.cur_row, dr, X16S_MAX_ROWS - 1),
              step(grid.cur_col, dc, X16S_MAX_COLS - 1));
}

/* A file command runs in two phases and each has its own overlay: the
 * dialog that chooses a name, then the code that reads or writes it. They
 * are swapped in turn rather than kept resident together. */
#define dialogs()  ovl_require(OVL_FILEDLG)
#define fileio()   ovl_require(OVL_FILEIO)      /* the .X16S reader */
#define filesave() ovl_require(OVL_X16S_SAVE)   /* and the writer   */

static void after_file_op(void)
{
    /* The sort undo points into the banked heap, and New, Open and Import
     * all throw that heap away and build another. A handle kept across
     * that addresses whatever the next allocation was given -- the exact
     * fault that once wrote column widths over the shared-string map. So
     * it goes here, where every file command ends up. */
    sort_undo_n = 0;

    /* The clipboard is a handle into that same heap. Zero the COUNT and
     * not the handle, exactly as above -- wb_reset() reclaims the whole
     * heap, so there is nothing left to free and everything to stop
     * trusting. */
    clip_rows = 0;

    /* A file just loaded is showing the values it was saved with, which
     * are its own. Nothing is stale, so RECALC would be a lie. */
    wb_calc_due = 0;
    grid_render_all();
}

/* Let the block go, and show that it has gone.
 *
 * COPY is what needs this. Until it did, the block stayed anchored after
 * copying and every arrow key grew it instead of moving the cursor -- so
 * the paste landed on the far corner of what had just been copied, and
 * read as nothing having happened at all.
 *
 * There is deliberately no Escape for it: Ctrl+A toggles, and copying
 * lets go by itself, which leaves nothing for a third way out to do. */
static void sel_drop(void)
{
    if (!grid.sel_on)
        return;
    grid.sel_on = 0;
    sel_bounds();
    render_grid();
    render_status();        /* BLOCK goes with it */
}

/* Load an overlay, and SAY SO IF IT WILL NOT.
 *
 * Use this rather than a bare ovl_require(): a plain `if (... == ERR_OK)`
 * leaves a failed load producing no action, no message and no clue, which
 * is indistinguishable from a command that did nothing. */
/* check_overlays.py finds what a file loads by looking for a literal
 * ovl_require(OVL_x), so routing four of them through need_ovl() hid them
 * and it reported grid.c calling into overlays 15 and 16 without loading
 * them. This is how a file says otherwise -- see xlsx_import.c, which has
 * the same problem for the same reason.
 *
 * ovl-loads: OVL_MENU OVL_FREF OVL_CHART OVL_CHARTOUT
 */
static char hex4(uint8_t v)
{
    return (char)(v < 10 ? '0' + v : 'A' - 10 + v);
}

static uint8_t need_ovl(uint8_t id)
{
    uint8_t y;

    if (ovl_require(id) == ERR_OK)
        return 1;

    y = (uint8_t)(screen_rows() - 1);

    /* The BASIC error code, in one digit: 1 too many files, 2 file open,
     * 3 file not open, 4 file not found, 5 device not present, 8 missing
     * file name, 9 illegal device number, A STOP key, B general I/O error.
     * E is a bad overlay id. See ovl_require(). */
    S_noovl[8] = hex4(ovl_status);

    screen_fill(0, y, screen_cols(), ' ', C_CELL);
    screen_text(1, y, S_noovl, (uint8_t)(sizeof S_noovl - 1), C_CELL);
    return 0;
}

/* Drive a reference rewriter to completion.
 *
 * The rewriter is in OVL_FREF and the compiler in OVL_FCOMPILE, so neither
 * can call the other and this sits between them. It also loops: the queue
 * holds FPEND_MAX entries and a big sheet has more formulas than that, so
 * "full" is an answer, not a failure. See formula.h.
 *
 * `what` picks the rewriter rather than three copies of this loop, because
 * three copies is what the resident image cannot afford. */
#define FREF_ROWS  0
#define FREF_COLS  1        /* must be 1: formula_shift()'s `cols` flag */
#define FREF_SORT  2
#define FREF_PASTE 3

static void fref_run(uint8_t what, uint16_t a, uint16_t b)
{
    uint8_t more = 1, first = 1;

    while (more) {
        /* A queue PER PASS: formula_compile_pending() frees it and nulls
         * the handle when it is done, which is right for the importers
         * that call it once and was an infinite loop here -- the second
         * pass found no queue, read that as "full", and asked again. */
        formula_pending = bank_alloc(FPEND_MAX * FPEND_FIELD);
        if (formula_pending == H_NULL)
            return;                     /* no room: leave them unadjusted */
        formula_pending_n = 0;

        if (ovl_require(OVL_FREF) != ERR_OK) {
            bank_free(formula_pending);
            formula_pending = H_NULL;
            return;
        }
        if (what <= FREF_COLS)
            more = formula_shift(a, (int8_t)b, what, first);
        else if (what == FREF_SORT)
            more = formula_sorted((uint8_t)a, first);
        else
            more = formula_paste(a, b, first);
        first = 0;
        formula_compile_pending(0);      /* loads OVL_FCOMPILE */
    }
}

/* Back to A1 and repaint. Whatever just happened -- a different sheet, a
 * deleted one -- the viewport may be scrolled past the end of what is there
 * now, and there is no cursor position worth preserving across it. */
static void go_home(void)
{
    grid.cur_row = grid.cur_col = 0;
    grid.top_row = grid.left_col = 0;
    grid_render_all();
}

/* Save to `cur_name`, asking for a name first if there is not one yet. */
static uint8_t is_xlsx(const char *name)
{
    uint8_t n = 0;

    while (name[n])
        ++n;
    if (n < 5)
        return 0;
    name += n - 5;
    return name[0] == '.' && (name[1] | 32) == 'x' && (name[2] | 32) == 'l'
                          && (name[3] | 32) == 's' && (name[4] | 32) == 'x';
}

/* One name buffer for every file command. They never run at the same time,
 * so one is enough -- and a local in each would be on the C stack, which
 * has about 127 bytes to spare at the bottom of an import. A filename is
 * not what to spend those on. */
X16S_GOLDEN_BEGIN
static char file_name[X16S_PATH_MAX];
X16S_GOLDEN_END

/* Do the work the command names, with the overlay that does it loaded.
 *
 * This is the only part of a file command that has to be resident: it
 * swaps overlays, and an overlay cannot. The conversation either side of it
 * is in OVL_FILEDLG — see filedlg_ask() and filedlg_report().
 */
static err_t run_command(uint8_t cmd)
{
    err_t e = ERR_OK;

    file_note.is_xlsx = 0;

    switch (cmd) {
    case FC_NEW:
        e = wb_reset();
        file_name[0] = '\0';
        break;

    case FC_OPEN:
        /* x16s_open loads the overlay, reads the file and compiles the
         * formulas it carried -- see workbook.c. */
        e = x16s_open(file_name);
        break;

    case FC_SAVE:
    case FC_SAVEAS:
        if (filesave() != ERR_OK)
            return ERR_IO;
        e = x16s_save(file_name);
        break;

    case FC_IMPORT:
#ifdef X16S_XLSX_UI
        if (is_xlsx(file_name)) {
            /* The importer swaps overlays itself. */
            file_note.is_xlsx = 1;
            e = xlsx_import(file_name);
            break;
        }
#endif
        if (ovl_require(OVL_CSV) != ERR_OK)
            return ERR_IO;
        {
            csv_result_t res;
            /* A1, not the cursor. Import means "read this file in", and an
             * .xlsx replaces the workbook outright -- a CSV landing wherever
             * the cursor happened to be made the same command do two
             * different things, with nothing on screen to say which. */
            e = csv_import(file_name, 0, 0, &res);
            /* The importer could not compile the formulas it found -- that
             * would have loaded an overlay over the one it was running in.
             * Here is where it is safe. See csv.c. */
            formula_compile_pending(0);
            file_note.rows      = res.rows;
            file_note.cells     = res.cells;
            file_note.truncated = res.truncated;
        }
        break;

    default:                            /* FC_EXPORT */
#ifdef X16S_XLSX_UI
        if (is_xlsx(file_name)) {
            e = xlsx_export(file_name);
            break;
        }
#endif
        if (ovl_require(OVL_CSV) != ERR_OK)
            return ERR_IO;
        e = csv_export(file_name, ',', 0);
        break;
    }
    return e;
}

/* Talk to the user, do the work, say how it went. Every file command is
 * that, and only the middle of it differs. */
static void do_file(uint8_t cmd)
{
    err_t e;

    if (dialogs() != ERR_OK)
        return;

    /* What the prompt starts with. Save reuses the workbook's own name;
     * Save as and Export SUGGEST one rather than opening empty -- the
     * workbook's if it has been saved, otherwise whatever the last command
     * named, which after an import is the file it came from. Everything
     * else starts blank, because it is about to choose a name of its own. */
    if (cmd == FC_SAVE)
        strcpy(file_name, cur_name);
    else if (cmd == FC_SAVEAS || cmd == FC_EXPORT) {
        if (cur_name[0])
            strcpy(file_name, cur_name);
    } else
        file_name[0] = '\0';

    if (!filedlg_ask(cmd, file_name, sizeof file_name)) {
        after_file_op();
        return;
    }

    e = run_command(cmd);

    if (dialogs() == ERR_OK)
        filedlg_report(cmd, e);

    if (e == ERR_OK) {
        switch (cmd) {
        case FC_OPEN:
        case FC_SAVE:
        case FC_SAVEAS:
            strcpy(cur_name, file_name);
            break;
        case FC_NEW:
            cur_name[0] = '\0';
            break;
        default:
            break;
        }
        if (cmd != FC_SAVE && cmd != FC_SAVEAS)
            go_home();
    } else if (cmd == FC_SAVEAS) {
        /* A failed Save As must not leave a name the user never got. */
        cur_name[0] = '\0';
    }
    after_file_op();
}

/* Finish an edit: store the text, then move the way the committing key
 * implies. Cancelling leaves the cell untouched. */
static void finish_edit(ed_result_t r)
{
    int16_t dr = 0, dc = 0;

    if (r != ED_CANCEL) {
        wb_set_text(grid.cur_row, grid.cur_col, editor_text());
        switch (r) {
        case ED_COMMIT_DOWN:  dr = 1;  break;
        case ED_COMMIT_UP:    dr = -1; break;
        case ED_COMMIT_RIGHT: dc = 1;  break;
        case ED_COMMIT_LEFT:  dc = -1; break;
        default: break;
        }
    }
    editor_stop();
    repaint_cell(grid.cur_row, grid.cur_col);
    if (dr || dc)
        grid_move(dr, dc);
    else
        render_formula_bar();
    render_status();
}

/* Which rows and columns the next sort will move.
 *
 * A selected block sorts as a block: only those rows move, and only within
 * those columns, so a table beside the data is left where it is. Without a
 * selection this is what sorting has always done -- the cursor's row down
 * to the last used one, every column -- because there is nothing else to
 * infer an intent from.
 *
 * Resident, and computed before either overlay loads, because sort_ask()
 * and sort_run() live in different ones and cannot share a static. */
void sort_range(void)
{
    const cellstore_t *cs = wb_cells();

    if (grid.sel_on) {
        sort_r0 = sel_r1;
        sort_r1 = sel_r2;
        sort_c0 = sel_c1;
        sort_c1 = sel_c2;
        /* The block's FIRST column, not the cursor's.
         *
         * The cursor is one corner of the selection, so after anchoring at
         * A1 and extending right it sits on the last column -- selecting
         * A:C would then sort by C, which is not what selecting a table
         * from its left edge means. The other columns follow, because the
         * whole block moves as rows. */
        sort_keycol = sel_c1;
    } else {
        sort_r0 = grid.cur_row;
        sort_r1 = cs->max_row;
        sort_c0 = 0;
        sort_c1 = cs->max_col;
        sort_keycol = grid.cur_col;
    }
}

grid_action_t grid_key(uint8_t key)
{
    /* While editing, the editor sees every key first: arrows move the caret
     * rather than the cursor, and Escape cancels rather than quitting. */
    if (editor_active()) {
        ed_result_t r = editor_key(key);
        if (r == ED_EDITING)
            render_formula_bar();
        else
            finish_edit(r);
        return GRID_CONTINUE;
    }

again:
    /* Charts, before the switch rather than as cases in it.
     *
     * They are the only commands living in an overlay the menu cannot load,
     * so they cannot run where they were chosen: OVL_CHART has to be
     * swapped in from resident code, which is here. A range test rather
     * than three more case labels, which cost more.
     *
     * chart_run() repaints the grid on its way out. */
    if (key >= K_CHART) {
        if (need_ovl(OVL_CHART)
            && chart_run((uint8_t)(key - K_CHART))
            && need_ovl(OVL_CHARTOUT))
            chart_bmp_save();       /* S was pressed; see chart.h */
        return GRID_CONTINUE;
    }

    switch (key) {
    case K_F10:
        /* The menu runs no commands: it hands back the key its choice
         * stands for and that is dispatched here, so there is exactly one
         * implementation of every command. */
        if (ovl_require(OVL_MENU) == ERR_OK) {
            key = menu_run();
            /* Repaint, but do NOT go through after_file_op(): that also
             * forgets the last sort, and the menu is how Undo sort is
             * reached. Choosing it cleared the record and then found
             * nothing to undo -- every time, while the host test passed
             * because it dispatched the key directly and never opened a
             * menu at all. */
            grid_render_all();          /* the dropdown covered cells */
            if (key)
                goto again;             /* not a call: the C stack is 512 */
        }
        break;

    case K_UP:                 grid_move(-1, 0); break;
    case K_DOWN:
    case K_RETURN:             grid_move(1, 0);  break;
    case K_LEFT:
    case K_SHIFTTAB:           grid_move(0, -1); break;
    case K_RIGHT:
    case K_TAB:                grid_move(0, 1);  break;
    case K_PGUP:               grid_move(-(int16_t)grid.grid_rows, 0); break;
    case K_PGDN:               grid_move((int16_t)grid.grid_rows, 0);  break;
    case K_HOME:               grid_goto(grid.cur_row, 0); break;
    case K_CLEAR:              go_home(); break;
    /* Until there is cell data, "last used column" is the last column.
     * Step 4 narrows this to the used range. */
    case K_END:                grid_goto(grid.cur_row, X16S_MAX_COLS - 1);
                               break;
    case K_F1: do_file(FC_NEW);    break;
    case K_F3: do_file(FC_OPEN);   break;
    case K_F4: do_file(FC_SAVE);   break;
    case K_F6: do_file(FC_SAVEAS); break;
    case K_F7: do_file(FC_IMPORT); break;
    case K_F8: do_file(FC_EXPORT); break;

    case K_SHEET_NEXT:
    case K_SHEET_PREV: {
        uint8_t n = wb_sheet_i;

        /* Comparisons rather than % : cc65 compiles a modulo into a runtime
         * helper call, and this only ever wraps by one. */
        if (key == K_SHEET_NEXT)
            n = (uint8_t)(n + 1 < wb_sheet_n ? n + 1 : 0);
        else
            n = (uint8_t)(n ? n - 1 : wb_sheet_n - 1);
        if (wb_sheet_switch(n) == ERR_OK)
            go_home();
        break;
    }

    case K_FREEZE: {
        uint16_t r, c;

        /* One command, not two: frozen already means unfreeze. Freezing
         * takes the cursor as the corner, the way Excel does -- put the
         * cursor on the first row of the data and in the first column of
         * it, and everything above and left of it stays put.
         *
         * Both are refused if they would leave nothing to scroll: a sheet
         * frozen down to its last visible row has no moving part, and no
         * way back except this command, which the user would then have to
         * find with the cursor somewhere it will not go. */
        if (grid.frz_row || grid.frz_col) {
            grid.frz_row = grid.frz_col = 0;
        } else {
            /* At A1 there is nothing above or left of the cursor, so the
             * corner rule would freeze nothing and leave both counts at 0
             * -- after which the next press is another attempt rather than
             * the unfreeze the user is now expecting. A command that does
             * nothing and cannot be undone reads as a broken one, so A1 is
             * taken to mean the obvious thing instead: keep the first row
             * and the first column. Anywhere else, the cursor is the
             * corner. */
            r = grid.cur_row ? grid.cur_row : 1;
            c = grid.cur_col ? grid.cur_col : 1;

            if (r < (uint16_t)(grid.grid_rows >> 1))
                grid.frz_row = (uint8_t)r;
            if (freeze_cols_fit(c))
                grid.frz_col = (uint8_t)c;
            grid.top_row = grid.frz_row;
            grid.left_col = grid.frz_col;

            /* The guards above refuse a corner that would leave nothing to
             * scroll -- a cursor far down the sheet, or so far right that
             * no whole column would be left. Both used to decline in
             * silence, which reads as a command that does not work. */
            if (!grid.frz_row && !grid.frz_col)
                grid_set_status(S_nofrz);
        }
        /* No ensure_visible(): the cursor is the corner it just froze at,
         * which is the first scrolling cell and so visible by definition. */
        grid_render_all();
        break;
    }

    case K_CALC_AUTO:
        /* Switching back to automatic recalculates at once if anything was
         * skipped while it was off -- the point of the switch is to defer
         * the work, not to lose it. Switching it OFF just stops. */
        wb_manual ^= 1;
        if (wb_manual)
            break;
        /* falls through */

    case K_CALC_NOW:
        /* Not wb_after_change(): that one obeys the switch, and this is the
         * command that overrides it. Resident, because loading OVL_FEVAL is
         * only safe from here. */
        wb_calc_due = 0;
        if (wb_has_formulas() && ovl_require(OVL_FEVAL) == ERR_OK)
            formula_recalc();
        grid_render_all();
        break;

    case K_INS_COL:
    case K_DEL_COL:
        /* OVL_FREF, not OVL_MENU where the row twin lives: that overlay
         * has two bytes free, and the reference shifting a column move
         * needs is in OVL_FREF already. */
        /* The cursor IS the cut and the key IS the direction, so there is
         * nothing for col_cmd() to hand back but "it happened" -- unlike
         * the row twin, which is in another overlay and has to. */
        if (need_ovl(OVL_FREF) && col_cmd(key == K_DEL_COL))
            fref_run(FREF_COLS, grid.cur_col,
                     key == K_DEL_COL ? (uint16_t)-1 : 1);
        grid_render_all();
        break;

    case K_COPY:
    case K_PASTE:
    case K_FIND:
    case K_REPLACE:
    case K_COL_W:
    case K_INS_ROW:
    case K_DEL_ROW:
        /* All three are in OVL_MENU, which is already the loaded overlay
         * when one of them came off a menu and is one load away after
         * Ctrl+F. All three ask for themselves on the bottom line, so
         * unlike every other command here they need no dialog overlay --
         * which is the only reason they fit. One call for the three, for
         * the same reason: see menu_cmd(). It says whether the sheet
         * actually changed, so Find costs no recalculation. */
        if (need_ovl(OVL_MENU) && menu_cmd(key)) {
            /* OVL_MENU wrote everything it could; a paste leaves its
             * formulas for the compiler, which is a second overlay and so
             * has to be loaded from here. */
            if (key == K_PASTE)
                fref_run(FREF_PASTE, grid.cur_row, grid.cur_col);
            /* The rows have moved; the formulas that pointed at them have
             * not. Adjusting is a second overlay, so it happens here --
             * OVL_MENU could not have called it. Before the recalculation,
             * which has to see the corrected references. */
            if (row_shift_by)
                fref_run(FREF_ROWS, row_shift_at, (uint16_t)row_shift_by);
            wb_after_change();
            grid_render_all();
        }
        /* Copying is finished with the block; anything else keeps it. */
        if (key == K_COPY)
            sel_drop();
        break;

    case K_SORT_ASC:
    case K_SORT_DESC: {
        /* Two overlays: the asking is with the dialogs, the work is where
         * there was room for it. See menu.h. */
        handle_t idx;

        sort_range();                   /* before either overlay loads */
        if (dialogs() != ERR_OK)
            break;
        idx = sort_ask();
        if (idx != H_NULL && ovl_require(OVL_CSV) == ERR_OK) {
            sort_run(idx, (uint8_t)(key == K_SORT_DESC));
            /* AFTER the rows moved: the permutation sort_run left says
             * where each one came from, which is what the references are
             * rewritten against. A second overlay, so it happens here. */
            fref_run(FREF_SORT, 0, 0);
            /* Here, not in sort_run: recalculating loads OVL_FEVAL, and
             * that would land on top of the overlay sort_run is running
             * in. Resident code is the only safe place to swap. */
            wb_after_change();
        }
        go_home();
        break;
    }

    case K_SORT_UNDO:
        /* No dialog, so no OVL_FILEDLG: straight to the overlay that has
         * the row swapping in it. */
        if (sort_undo_n) {
            /* BEFORE the rows move, unlike the sort above: sort_undo()
             * frees the permutation, and the references are rewritten
             * against it. The rows have not moved yet, but every formula
             * that is about to move is already at the row this reads. */
            fref_run(FREF_SORT, 1, 0);
            if (ovl_require(OVL_CSV) == ERR_OK)
                sort_undo();
            wb_after_change();          /* see the sort case above */
        }
        go_home();
        break;

    case K_SHEET_ADD:
    case K_SHEET_DEL:
    case K_SHEET_REN:
        /* OVL_FILEDLG, not OVL_MENU: these ask a question, and the asking
         * and the doing have to be in the same overlay. */
        if (dialogs() == ERR_OK)
            sheet_command(key);
        go_home();
        break;

    case K_F9:
        /* Refuse to lose work silently. */
        if (wb_dirty) {
            if (dialogs() == ERR_OK
                && !dlg_discard(DC_QUIT)) {
                after_file_op();
                break;
            }
        }
        return GRID_QUIT;

    case K_SELECT:
        /* Anchor here, or drop the block if there is one. Movement does
         * the rest: the anchor stays and the cursor is the far corner. */
        grid.sel_on = (uint8_t)!grid.sel_on;
        grid.sel_row = grid.cur_row;
        grid.sel_col = grid.cur_col;
        sel_bounds();
        render_grid();
        render_status();    /* which shows or clears BLOCK */
        break;

    case K_DEL:
    case K_FWDDEL: {
        /* The block if there is one, the cell if not -- grid_sel() answers
         * both the same way, so there is one path here.
         *
         * wb_clear() recalculates on every call, which for a block would
         * be once a cell. The manual-recalc switch is exactly the lever
         * for that: hold it down, clear the lot, put it back, recalculate
         * once. wb_after_change() then does nothing if it was already off,
         * which is right -- the user asked for that. */
        uint16_t r, c;
        uint8_t was = wb_manual;

        wb_manual = 1;
        for (r = sel_r1; r <= sel_r2; ++r)
            for (c = sel_c1; c <= sel_c2; ++c)
                wb_clear(r, c);
        wb_manual = was;
        wb_after_change();

        sel_drop();
        grid_render_all();
        break;
    }

    case K_F2: {
        /* Edit in place: start from the value that is already there. */
        char text[WB_TEXT_MAX];
        wb_edit_text(grid.cur_row, grid.cur_col, text, sizeof text);
        editor_start(text);
        render_formula_bar();
        render_status();        /* READY -> EDIT */
        break;
    }

    case K_F5:
        wb_undo();
        /* An undo can restore a cell anywhere in the sheet, including one
         * that is no longer on screen, so repaint rather than patch. */
        render_grid();
        render_formula_bar();
        render_status();
        break;

    case K_ESC:
        break;                  /* only meaningful while editing */

    default:
        /* Typing a printable character replaces the cell, the way it does
         * in every spreadsheet: the keystroke becomes the first character
         * of the new value rather than being swallowed. */
        if (key >= 0x20 && key < 0x7F) {
            char first[2];
            first[0] = (char)key;
            first[1] = '\0';
            editor_start(first);
            render_formula_bar();
            render_status();    /* READY -> EDIT */
        }
        break;
    }
    return GRID_CONTINUE;
}

/* --- the pointer -------------------------------------------------- *
 *
 * Kept here, in arithmetic over screen.h, for the reason at the top of
 * grid.h: the interesting part of a click is working out which cell it
 * landed on, and that is testable on the host. src/platform/mouse_x16.s
 * has everything that is actually a mouse.
 */

/* Which column covers screen column `sx`, walking the widths from the left
 * of the viewport. There is no closed form -- columns have individual
 * widths -- and this is the inverse of grid_col_x().
 *
 * A click past the last drawable column lands on that column rather than
 * nowhere: the rightmost column is clipped by the screen edge, not hidden,
 * so the few characters of it that are visible have to be clickable. */
static uint16_t col_at(uint8_t sx)
{
    uint16_t i = 0, c;
    uint8_t  x = GRID_ROWHDR_W;

    for (;;) {
        uint8_t w = col_w(c = col_nth(i));
        if (sx < (uint8_t)(x + w))
            return c;
        x = (uint8_t)(x + w);
        if (x >= screen_cols())
            return c;
        ++i;
    }
}

/* A character is 8 pixels unless the mode is one of the half-width or
 * half-height ones, since the display is 640x480 however many characters
 * are laid over it. Worked out once in grid_init() rather than per click:
 * these are a shift of 0 or 1 applied after the constant shift of 3, which
 * is an lsr, where letting the amount vary would have made cc65 emit a
 * helper call and a loop at every use. */
X16S_GOLDEN_BEGIN
static uint8_t narrow_ch, short_ch;
X16S_GOLDEN_END

grid_action_t grid_mouse(uint16_t px, uint16_t py, uint8_t buttons,
                         int8_t wheel)
{
    /* Edge-triggered: a click is the transition to held, not the holding.
     * Without this a button left down would re-fire on every pass of the
     * main loop, which for the menu bar means reopening the menu under
     * itself several hundred times a second. */
    X16S_GOLDEN_BEGIN
    static uint8_t was_down;
    X16S_GOLDEN_END
    uint8_t down = (uint8_t)(buttons & MOUSE_LEFT);
    uint8_t fired = (uint8_t)(down && !was_down);
    uint8_t sx, sy, tabs;

    was_down = down;

    /* The editor owns the keyboard while it is open and the same applies
     * here. Committing an edit because the pointer strayed would lose the
     * line being typed, and there is nowhere on screen a click could
     * usefully go that Escape and Return do not already reach. */
    if (editor_active())
        return GRID_CONTINUE;

    if (wheel) {
        /* Positive is towards the user, which scrolls the sheet down.
         * Three rows a notch: one is imperceptible and a screenful
         * overshoots. */
        grid_move(wheel > 0 ? 3 : -3, 0);
        return GRID_CONTINUE;
    }

    if (!fired)
        return GRID_CONTINUE;

    sx = (uint8_t)(px >> 3);
    if (narrow_ch)
        sx >>= 1;
    sy = (uint8_t)(py >> 3);
    if (short_ch)
        sy >>= 1;

    if (sy == ROW_MENU) {
        /* Which menu, so clicking "Edit" opens Edit rather than whichever
         * one F10 happens to open first. menu_run() consumes it.
         *
         * The product name at the right-hand end is a button too, and takes
         * the same route so that this needs no second path: MENU_ABOUT is a
         * column number no bar is ever that wide to have. */
        menu_open_x = sx >= (uint8_t)(screen_cols() - sizeof S_name)
                    ? MENU_ABOUT : sx;
        return grid_key(K_F10);         /* the same menu the key opens */
    }

    /* Held in a local: ROW_TABS is a macro over screen_rows(), so each
     * mention of it is another call. */
    tabs = ROW_TABS;

    if (sy == tabs) {
        /* Which tab: fixed width, but TAB_W is 10 and cc65 compiles a
         * division by a non-power of two into a helper call, so this walks
         * the same way render_tabs() lays them out. */
        uint8_t i, x = 0;
        for (i = 0; i < wb_sheet_n; ++i) {
            if (sx < (uint8_t)(x + TAB_W)) {
                /* go_home() repaints the lot, tabs included, so the newly
                 * selected one is highlighted without asking. */
                if (i != wb_sheet_i && wb_sheet_switch(i) == ERR_OK)
                    go_home();
                break;
            }
            x = (uint8_t)(x + TAB_W);
        }
        return GRID_CONTINUE;
    }

    /* A cell. The row header is not a target -- clicking it would have to
     * mean select-the-row, and there is no row selection to mean it with. */
    if (sy >= ROW_GRID0 && sy < tabs && sx >= GRID_ROWHDR_W)
        grid_goto(row_at((uint8_t)(sy - ROW_GRID0)), col_at(sx));

    return GRID_CONTINUE;
}


void grid_set_status(const char *text) { status_text = text; }

err_t grid_init(void)
{
    memset(&grid, 0, sizeof grid);

#ifndef __CC65__
    /* The clipboard is program state and survives File > New on purpose,
     * but a fresh grid means a fresh heap, so the handle it holds is not
     * ours any more.
     *
     * Host only, and not for tidiness: on the machine grid_init() runs
     * once, at startup, when golden RAM has just been zeroed and there is
     * nothing to forget -- every later heap rebuild goes through
     * after_file_op(), which clears this. The host suite rebuilds the heap
     * under a live clipboard in every setup(), which the machine never
     * does. Five resident bytes, and they were not there. */
    clip_rows = 0;
#endif


    /* Six rows are not the grid: menu, formula bar, column header, tabs
     * and status are five, and the last row of the screen is left blank for
     * prompts and messages. */
    if (screen_rows() < 10)
        return ERR_VALUE;
    grid.grid_rows = (uint8_t)(screen_rows() - 6);

    /* Character size, for turning a pointer position into a cell. See
     * grid_mouse(). */
    narrow_ch = (uint8_t)(screen_cols() < 80);
    short_ch  = (uint8_t)(screen_rows() < 60);

    return ERR_OK;
}
