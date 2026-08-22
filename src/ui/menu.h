/* menu.h — the menu bar, opened with F10.
 *
 * Lives in OVL_MENU, which is the whole point: drawing a bar, tracking a
 * highlight and reading arrow keys is a few hundred bytes, and the resident
 * core has none to spare. What stays resident is one case in grid_key().
 *
 * It runs no commands. menu_run() returns the key code of whatever was
 * chosen and the caller dispatches it exactly as if that key had been
 * pressed, so every command has one implementation and the menu cannot
 * drift out of step with the keyboard.
 */
#ifndef X16S_MENU_H
#define X16S_MENU_H

#include "../x16sheet.h"

/* Open the bar. Returns the key code of the chosen command, or 0 if the
 * user cancelled. The caller must repaint: the dropdown covers cells. */
uint8_t menu_run(void);

/* Which menu to open, as the screen column the pointer was over, or
 * MENU_BY_KEY when it was F10 rather than a click.
 *
 * RESIDENT, defined in grid.c: the grid writes it before loading this
 * overlay and the overlay reads it afterwards, so a byte in OVL14's own bss
 * would be written through a window holding a different overlay.
 *
 * menu_run() resets it to MENU_BY_KEY as soon as it has read it, so a click
 * sets it for exactly one opening. */
#define MENU_BY_KEY 0xFF

/* ...or MENU_ABOUT when the click was on the product name at the right-hand
 * end of the bar, which is a button rather than a menu title. It travels the
 * same way a column does so that the grid needs no second path for it. */
#define MENU_ABOUT  0xFE
extern uint8_t menu_open_x;

/* Sheet commands the menu can return. Not codes the keyboard produces --
 * the X16 sends nothing above $9D -- so grid_key() can dispatch them the
 * same way it dispatches a key. Kept in step with the K_SHEET_* names in
 * grid.c. */
#define MENU_SHEET_NEXT 0xF0
#define MENU_SHEET_PREV 0xF1
#define MENU_SHEET_ADD  0xF2
#define MENU_SHEET_DEL  0xF3
#define MENU_SHEET_REN  0xF4
#define MENU_SORT_ASC   0xF5
#define MENU_SORT_DESC  0xF6
#define MENU_SORT_UNDO  0xF7
#define MENU_INS_ROW    0xF8
#define MENU_DEL_ROW    0xF9
#define MENU_CALC_NOW   0xFA
#define MENU_CALC_AUTO  0xFB
#define MENU_FREEZE     0xFC
/* The three chart commands, consecutive and in CHART_* order so the type
 * is the difference. Dispatched by the grid, not run by the menu: charts
 * are in OVL_CHART now and an overlay cannot load another. */
/* Not in the $F0 block with the rest: that is full. The X16 keyboard
 * sends nothing above $9D, so $9E is as safe a stand-in as $F0 was. */
#define MENU_BOLD       0x9C
#define MENU_BORDER     0x9A
#define MENU_UNDER      0x9B
#define MENU_COL_W      0x9E
#define MENU_REPLACE    0x9F

/* Below the control codes the keyboard produces. $F0-$FF is full and $9E
 * and $9F are taken; the X16 sends neither of these. */
#define MENU_INS_COL    0x1E
#define MENU_DEL_COL    0x1F

#define MENU_CHART      0xFD
#define MENU_CHART_LINE 0xFE
#define MENU_CHART_PIE  0xFF

/* Carry out one of the three that need a prompt or a confirmation.
 *
 * Runs in OVL_FILEDLG, not here: it asks a question first, and an overlay
 * cannot call into another overlay. The grid loads that overlay, calls
 * this, and repaints. */
void sheet_command(uint8_t cmd);

/* "Working" -- put up by filedlg_ask() before a command that takes tens of
 * seconds, and left on screen for the whole of it. In OVL_FILEDLG. */
void dlg_busy(const char *title, const char *name);

/* Sorting, in two halves and two overlays.
 *
 * sort_ask() is in OVL_FILEDLG with the dialogs: it checks the range, warns
 * that formulas will not have their references adjusted, and allocates the
 * banked index the sort needs -- there, so that running out of memory can
 * be reported. H_NULL means do not sort.
 *
 * sort_run() is in OVL_CSV, which had the room. It takes the index, sorts
 * the used range by the cursor's column, and frees it. The caller must be
 * resident and must load each overlay in turn. */
/* Find: prompts on the bottom line and walks the sheet for what was typed,
 * all inside OVL_MENU. It asks for itself rather than using dlg_prompt,
 * which would mean loading a second overlay. Nothing matching, or Escape,
 * leaves the cursor where it was. */
void find_run(void);
extern char find_text[16];
extern char rep_text[16];

/* The clipboard. Defined in grid.c (golden RAM) because OVL_MENU fills it
 * and OVL_FCOMPILE reads it. clip_rows is the guard: a handle that
 * survived File > New addresses whatever was allocated after it. */
extern handle_t clip_h;
extern uint16_t clip_rows, clip_cols;
extern uint16_t clip_row0, clip_col0;

/* The codes copy and paste already answer to, so the menu and the keyboard
 * agree without a second set. */
#define MENU_COPY   0x03
#define MENU_PASTE  0x16

/* Find, insert row and delete row, through one door.
 *
 * All three are in OVL_MENU and all three ask for themselves, so the
 * resident side of each is identical: load the overlay, call in. Three
 * case labels sharing one call rather than three copies of it, because the
 * dispatch is exactly where the free space is not.
 *
 * Returns 1 if the sheet changed, which is the caller's cue to recalculate
 * and repaint -- Find never does, and a recalculation it did not need is
 * seconds of the user's time on a sheet full of formulas. */
uint8_t menu_cmd(uint8_t key);

/* Where the last insert or delete happened, and which way. Set by
 * row_cmd() in OVL_MENU and read by the grid, because adjusting the
 * formulas means loading OVL_FCOMPILE and an overlay cannot load another.
 * Resident for the same reason sort_undo_* is. */
extern uint16_t row_shift_at;
extern int8_t   row_shift_by;


handle_t sort_ask(void);

/* The first row a sort moves: where the cursor is, so a heading row is left
 * alone.
 *
 * A MACRO because both halves of the sort need it and they live in
 * different overlays -- as a function it would be a cross-overlay call.
 * grid_state() is resident, so inlining costs nothing. */
/* The rows and columns a sort will move, worked out in resident code
 * before either overlay is loaded -- sort_ask() is with the dialogs and
 * sort_run() is with the work, and neither can see the other's statics.
 *
 * With a block selected these are its bounds; without one they are the
 * cursor's row down to the last used one, across every column, which is
 * what sorting did before there was a selection to ask about. */
extern uint16_t sort_r0, sort_r1, sort_c0, sort_c1;
/* The column whose values decide the order. */
extern uint16_t sort_keycol;
void sort_range(void);

#define sort_base() (sort_r0)
void     sort_run(handle_t idx, uint8_t desc);

/* What the last sort did, kept so it can be taken back.
 *
 * The permutation, not a copy of the rows: two bytes a row against eight a
 * cell, and exact rather than reconstructed -- undoing is the same row
 * swapping run through the inverse. A twelve-hundred-row sheet costs 2400
 * bytes to be able to undo instead of 57 KB.
 *
 * Resident (golden RAM, defined in grid.c) because sort_run writes it and
 * sort_undo reads it, and OVL_CSV's own bss does not survive the overlay
 * being swapped out in between. Cleared by anything that invalidates it --
 * see after_file_op() -- because the handle is into a heap that File > New
 * and File > Open throw away. */
extern handle_t sort_undo_idx;
extern uint16_t sort_undo_n;
extern uint8_t  sort_undo_sheet;
extern uint16_t sort_undo_base;
/* The columns that moved, so undo restores the same block and no more. */
extern uint16_t sort_undo_c0, sort_undo_c1;
extern uint16_t sort_undo_maxrow;

/* Put the rows back. Does nothing unless the sheet and its size still match
 * what was sorted. */
void sort_undo(void);

#endif /* X16S_MENU_H */
