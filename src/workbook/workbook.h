/* workbook.h — the model the UI talks to.
 *
 * Everything below this line (cells, strings, styles, the banked heap) is an
 * implementation detail. The grid asks for text and gets text.
 *
 * Reference counting lives here rather than in cells.c, because ownership is
 * a property of the edit operation, not of the container: overwriting a text
 * cell releases its string, undoing that edit takes it back.
 */
#ifndef X16S_WORKBOOK_H
#define X16S_WORKBOOK_H

#include "cells.h"
#include "styles.h"

err_t wb_init(void);

/* Discard everything and start over, reclaiming the whole banked heap.
 * Used by New and before a Load: the allocator does not compact, so the only
 * way to give a whole workbook's memory back is to reset the heap. */
err_t wb_reset(void);

/* Enough for any cell rendering, including the widest number format. */
#define WB_TEXT_MAX 64

uint8_t wb_get(uint16_t row, uint16_t col, cell_record_t *out);

/* One column's display width in characters, clamped to
 * X16S_MIN_COL_W..X16S_MAX_COL_W. Out-of-range columns read as the default
 * and ignore writes. Reset with the workbook, saved with it, and set by the
 * .xlsx importer from <cols>. */
uint8_t wb_col_width(uint16_t col);

/* --- sheets ---------------------------------------------------------
 *
 * All of them live in banked RAM and only the active one is resident, so
 * everything else in this header goes on meaning "the active sheet" and
 * needs no sheet argument. See the note in workbook.c.
 *
 * Names are up to X16S_MAX_SHEET_NAME characters; wb_sheet_name() wants a
 * buffer of that plus one. */
/* How many there are and which is active. Exposed as variables rather than
 * behind accessors: they are one byte each, the tab row reads both on every
 * repaint, and a function call to fetch a byte is most of the cost. */
/* Recalculate after every change, or only when asked.
 *
 * `wb_manual` rather than `wb_auto` so that the default is zero: see the
 * note beside the definition. `wb_calc_due` says a change has been made
 * that has not been recalculated -- the status bar shows it, and switching
 * back to automatic acts on it. */
extern uint8_t wb_manual;
extern uint8_t wb_calc_due;

extern uint8_t wb_sheet_n;
extern uint8_t wb_sheet_i;

/* The name, truncated to what a tab shows. `out` needs WB_TAB_NAME bytes.
 *
 * The full name is read by wb_sheet_name() in the menu's overlay, where the
 * rename dialog is. The difference is twenty bytes in whatever buffer the
 * caller passes, and only the tab row needs one on every repaint. */
#define WB_TAB_NAME 10
void    wb_tab_name(uint8_t n, char *out);

/* All of it. `out` needs X16S_MAX_SHEET_NAME + 1. */
void    wb_sheet_name(uint8_t n, char *out);

/* ERR_NOTFOUND for a sheet that is not there. Switching to the active one
 * is not an error and does nothing. */
err_t   wb_sheet_switch(uint8_t n);
err_t   wb_sheet_rename(uint8_t n, const char *name);

/* ERR_LIMIT at X16S_MAX_SHEETS, and for deleting the last one — a workbook
 * with no sheets has nowhere to put a cell. Adding does not switch. */
err_t   wb_sheet_add(const char *name);
err_t   wb_sheet_delete(uint8_t n);
void    wb_set_col_width(uint16_t col, uint8_t w);

/* Interpret `text` the way a spreadsheet does — empty clears the cell,
 * TRUE/FALSE become booleans, anything that parses as a number becomes one,
 * everything else is text — and store the result. */
err_t wb_set_text(uint16_t row, uint16_t col, const char *text);

/* Store a cell directly. Used by importers, which already know the type. */
err_t wb_set(uint16_t row, uint16_t col, const cell_record_t *rec);

err_t wb_clear(uint16_t row, uint16_t col);

/* What the cell looks like in the grid: formatted by its style. */
uint8_t wb_display_text(uint16_t row, uint16_t col, char *out, uint8_t max);

/* What the cell looks like in the formula bar and when editing: the value as
 * the user would type it, without number formatting applied. */
uint8_t wb_edit_text(uint16_t row, uint16_t col, char *out, uint8_t max);

/* 1 when the cell should be right-aligned. */
uint8_t wb_align_right(const cell_record_t *rec);

/* Whether the cell's style asks for bold. Beside wb_align_right() because
 * it answers the same kind of question and the renderer asks both. */
/* The cell's style flags, whole: STY_BOLD, the alignment, the borders and
 * the rule bit.
 *
 * One accessor rather than one per bit. Each call reads the style out of
 * banked RAM, and the renderer wants three of these about every cell it
 * draws -- and workbook.c is compiled at --codesize 90, where a function
 * costs far more than the code in it. Mask at the call site. */
uint8_t wb_flags(const cell_record_t *rec);
#define wb_bold(rec) ((uint8_t)(wb_flags(rec) & STY_BOLD))

/* One level, as specified. Undoing does not itself become undoable. */
/* Recalculate after an edit. Does nothing on a sheet with no formulas, so
 * the common case never touches the overlay. */
void    wb_after_change(void);
uint8_t wb_has_formulas(void);

err_t   wb_undo(void);
#ifndef __CC65__       /* host only -- see the definition */
uint8_t wb_can_undo(void);
#endif

/* Whether anything has changed since the last save. A variable rather than
 * a pair of accessors, for the same reason as wb_sheet_n: it is one byte
 * and the call was most of the cost. */
extern uint8_t wb_dirty;

const cellstore_t *wb_cells(void);

#endif /* X16S_WORKBOOK_H */
