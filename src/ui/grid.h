/* grid.h — the spreadsheet view: headers, cell cursor, scrolling.
 *
 * Portable on purpose. Everything here is arithmetic over screen.h, so the
 * column positions, right-edge clipping and scroll behaviour are all
 * asserted in the host test suite rather than eyeballed in an emulator.
 *
 * Screen layout, for an 80x60 display:
 *
 *    0  menu bar
 *    1  formula bar          A1  =SUM(B1:B10)
 *    2  column headers           A         B         C
 *    3  first data row       1
 *   ..  ...                              54 rows
 *   56  last data row       54
 *   57  sheet tabs          Sheet1
 *   58  status bar          READY  A1  516088 bytes free
 *   59  message line
 */
#ifndef X16S_GRID_H
#define X16S_GRID_H

#include "screen.h"

/* Five digits for the row number plus one column of separator. Anything
 * narrower clips row 65535, which is exactly the row that must render. */
#define GRID_ROWHDR_W    6
#define GRID_ROWLBL_W    5
#define GRID_DEF_COL_W  X16S_DEF_COL_W

typedef struct {
    uint16_t cur_row, cur_col;    /* cell cursor, 0-based                 */
    uint16_t top_row, left_col;   /* first SCROLLING cell of the viewport */
    uint8_t  grid_rows;           /* data rows that fit on this screen    */

    /* Frozen headings: rows 0..frz_row-1 and columns 0..frz_col-1 stay on
     * screen while the rest scrolls under them. Zero for neither.
     *
     * top_row and left_col are then the first row and column of the
     * SCROLLING part, and both are >= their frozen count -- so a frozen
     * row is never also shown as a scrolled one. Nothing outside grid.c
     * should do the arithmetic itself; row_at(), col_nth() and
     * grid_col_x() are what know about this.
     *
     * A byte each. A heading block is one to three rows deep in every
     * sheet anyone actually writes, and it has to leave room to scroll in
     * regardless. */
    uint8_t  frz_row, frz_col;

    /* A selected block: the rectangle between this anchor and the cursor,
     * inclusive, when sel_on. Anchor and cursor rather than two corners,
     * because then moving the cursor grows the block and every existing
     * movement key works unchanged. */
    uint16_t sel_row, sel_col;
    uint8_t  sel_on;
} grid_t;

typedef enum {
    GRID_CONTINUE = 0,
    GRID_QUIT
} grid_action_t;

err_t grid_init(void);
void  grid_render_all(void);

/* The selected block, or the cursor cell alone when nothing is selected --
 * so a command can act on "the selection" without asking whether there is
 * one. Inclusive at both ends.
 *
 * The four are exported as well, because OVL_MENU reads them directly for
 * copy and paste and a call per cell would not be worth it -- which is
 * also why the call itself is host only. */
#ifndef __CC65__
void grid_sel(uint16_t *r1, uint16_t *c1, uint16_t *r2, uint16_t *c2);
#endif
extern uint16_t sel_r1, sel_c1, sel_r2, sel_c2;

/* Move the cursor, scrolling if needed. Repaints only what changed: two
 * cells for a move inside the viewport, the whole grid when it scrolls. */
void grid_goto(uint16_t row, uint16_t col);
void grid_move(int16_t dr, int16_t dc);

/* Handle one PETSCII key from the KERNAL. */
grid_action_t grid_key(uint8_t key);

/* Handle the pointer: `px`/`py` in pixels from the top left, `buttons` as
 * mouse.h defines them, `wheel` as a signed delta since the last call.
 *
 * Called every time round the main loop rather than on a change, because
 * the wheel is a delta and a skipped call loses it. Cheap when nothing has
 * happened: no button edge and no wheel movement returns immediately.
 *
 * Takes its inputs as arguments rather than reading mouse.h's variables so
 * that the host suite can drive it, which is the whole reason the
 * hit-testing lives in grid.c and not beside the KERNAL calls. */
grid_action_t grid_mouse(uint16_t px, uint16_t py, uint8_t buttons,
                         int8_t wheel);

/* The cursor and the viewport. A variable and a macro rather than an
 * accessor: it returned the address of a fixed object, and the three
 * overlays that ask for it were paying a call to be told a constant. */
extern grid_t grid;
#define grid_state() ((const grid_t *)&grid)


/* "A1", "IV65535". `out` needs 9 bytes. */
void grid_cell_name(uint16_t row, uint16_t col, char *out);
void grid_col_name(uint16_t col, char *out);

/* Screen x of a column, or -1 when it is scrolled off or fully clipped. */
int16_t grid_col_x(uint16_t col);

/* Free-memory figure shown in the status bar; set by the caller so the UI
 * layer keeps no dependency on the allocator. */
void grid_set_status(const char *text);

#endif /* X16S_GRID_H */
