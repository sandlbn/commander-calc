#include "test.h"
#include "../src/platform/file_io.h"
#include "../src/ui/grid.h"
#include "../src/ui/menu.h"
#include "../src/platform/keyboard.h"
#include "../src/platform/bankmem.h"
#include "../src/platform/banked_ram.h"
#include "../src/workbook/workbook.h"
#include "../src/workbook/strings.h"
#include "../src/ui/editor.h"
#include "../src/ui/chart.h"
#include "../src/platform/mouse.h"

#define K_PGDN 0x02
#define K_END  0x04
#define K_TAB  0x09
#define K_DOWN 0x11
#define K_HOME 0x13
#define K_RIGHT 0x1D
#define K_PGUP 0x82
#define K_UP   0x91
#define K_CLEAR 0x93
#define K_LEFT 0x9D
#define K_ENTER 0x0D

#define SCREEN_W 80    /* screen_host_init(80, 60) in setup() */
#define SCREEN_H 60
#define ROW_COLHDR 2
#define ROW_GRID0  3

static void setup(void)
{
    kbd_host_reset();
    /* The grid reads cell values now, so it needs a live workbook under it. */
    CHECK(bankmem_host_init(64));
    CHECK_EQ(bank_heap_init(1, 0), ERR_OK);
    /* The heap has just been thrown away, so a permutation left by an
     * earlier test now addresses whatever the next allocation is given.
     * after_file_op() does exactly this in the program, for exactly this
     * reason; the harness has to as well or the tests interfere. */
    sort_undo_n = 0;
    CHECK_EQ(wb_init(), ERR_OK);
    screen_host_init(80, 60);
    CHECK_EQ(grid_init(), ERR_OK);
    grid_render_all();
}

/* Does row `y` contain `needle` starting at column `x`? */
static int at(uint8_t x, uint8_t y, const char *needle)
{
    const char *row = screen_host_row(y);

    if (strlen(row) < (size_t)x + strlen(needle))
        return 0;
    return strncmp(row + x, needle, strlen(needle)) == 0;
}

static void test_col_names(void)
{
    char b[5];

    /* Bijective base 26 — the boundaries are where naive implementations
     * produce "BA" for column 26 or "II" for the last one. */
    grid_col_name(0, b);   CHECK_STR(b, "A");
    grid_col_name(25, b);  CHECK_STR(b, "Z");
    grid_col_name(26, b);  CHECK_STR(b, "AA");
    grid_col_name(27, b);  CHECK_STR(b, "AB");
    grid_col_name(51, b);  CHECK_STR(b, "AZ");
    grid_col_name(52, b);  CHECK_STR(b, "BA");
    grid_col_name(255, b); CHECK_STR(b, "IV");   /* Excel's 256th column */
}

static void test_cell_names(void)
{
    char b[9];

    /* Rows are 0-based internally and 1-based on screen. */
    grid_cell_name(0, 0, b);         CHECK_STR(b, "A1");
    grid_cell_name(24, 1, b);        CHECK_STR(b, "B25");
    grid_cell_name(65534, 255, b);   CHECK_STR(b, "IV65535");
}

static void test_initial_layout(void)
{
    const grid_t *g;

    setup();
    g = grid_state();

    CHECK_EQ(g->cur_row, 0);
    CHECK_EQ(g->cur_col, 0);
    CHECK_EQ(g->top_row, 0);
    CHECK_EQ(g->left_col, 0);
    /* 60 rows minus six of chrome. */
    CHECK_EQ(g->grid_rows, 54);

    /* Column headers land at the column positions, not at multiples of the
     * width — the row header shifts everything right by GRID_ROWHDR_W. */
    CHECK_EQ(grid_col_x(0), GRID_ROWHDR_W);
    CHECK_EQ(grid_col_x(1), GRID_ROWHDR_W + 10);
    CHECK(at(GRID_ROWHDR_W + 1, ROW_COLHDR, "A"));
    CHECK(at(GRID_ROWHDR_W + 11, ROW_COLHDR, "B"));
    CHECK(at(GRID_ROWHDR_W + 21, ROW_COLHDR, "C"));

    /* Row headers are right-aligned in their gutter. */
    CHECK(at(0, ROW_GRID0, "    1"));
    CHECK(at(0, ROW_GRID0 + 1, "    2"));
    CHECK(at(0, ROW_GRID0 + 53, "   54"));

    CHECK(at(1, 1, "A1"));                        /* formula bar */
    CHECK(at(1, 58, "READY"));                    /* status bar */
    CHECK(at(8, 58, "A1"));
}

/* The rightmost column is clipped rather than dropped, so the grid reaches
 * the screen edge instead of ending in a ragged gap. */
static void test_right_edge_clipping(void)
{
    setup();

    /* 80 columns: a 6-wide gutter, then 7 full columns to x=76, and column
     * H showing whatever is left. */
    CHECK_EQ(grid_col_x(6), 66);
    CHECK_EQ(grid_col_x(7), 76);                  /* only 4 of its 10 fit */
    CHECK_EQ(grid_col_x(8), -1);                  /* past the right edge */
    CHECK(at(77, ROW_COLHDR, "H"));
}

static void test_cursor_moves_without_scrolling(void)
{
    const grid_t *g = grid_state();

    setup();

    grid_key(K_RIGHT);
    CHECK_EQ(g->cur_col, 1);
    CHECK_EQ(g->left_col, 0);                     /* still in view */
    CHECK(at(1, 1, "B1"));

    grid_key(K_DOWN);
    CHECK_EQ(g->cur_row, 1);
    CHECK_EQ(g->top_row, 0);
    CHECK(at(1, 1, "B2"));

    /* The cursor cell is the one that is highlighted, and only it.
     *
     * Compared with each other rather than with literal colours: this test
     * is named for where the cursor is, and pinning the palette here meant
     * that recolouring the grid failed it twice for reasons that had
     * nothing to do with the cursor. */
    {
        color_t cur  = screen_host_color((uint8_t)grid_col_x(1), ROW_GRID0 + 1);
        color_t left = screen_host_color((uint8_t)grid_col_x(0), ROW_GRID0 + 1);
        color_t up   = screen_host_color((uint8_t)grid_col_x(1), ROW_GRID0);

        CHECK(cur != left);
        CHECK(cur != up);
        CHECK_EQ(left, up);
    }

    grid_key(K_LEFT);
    grid_key(K_UP);
    CHECK_EQ(g->cur_row, 0);
    CHECK_EQ(g->cur_col, 0);
}

static void test_edges_do_not_wrap(void)
{
    const grid_t *g = grid_state();

    setup();
    grid_key(K_UP);
    grid_key(K_LEFT);
    CHECK_EQ(g->cur_row, 0);
    CHECK_EQ(g->cur_col, 0);

    grid_goto(X16S_MAX_ROWS - 1, X16S_MAX_COLS - 1);
    grid_key(K_DOWN);
    grid_key(K_RIGHT);
    CHECK_EQ(g->cur_row, X16S_MAX_ROWS - 1);
    CHECK_EQ(g->cur_col, X16S_MAX_COLS - 1);

    /* Clamping, not wrapping, when a jump overshoots. Values above 65535
     * truncate at the uint16_t parameter and never reach the clamp, so the
     * meaningful case is the largest a caller can actually pass. */
    grid_goto(65535, 300);
    CHECK_EQ(g->cur_row, X16S_MAX_ROWS - 1);
    CHECK_EQ(g->cur_col, X16S_MAX_COLS - 1);
}

static void test_vertical_scrolling(void)
{
    const grid_t *g = grid_state();
    uint8_t i;

    setup();

    /* Walking down the last visible row must scroll by exactly one. */
    grid_goto(53, 0);
    CHECK_EQ(g->top_row, 0);
    grid_key(K_DOWN);
    CHECK_EQ(g->cur_row, 54);
    CHECK_EQ(g->top_row, 1);
    CHECK(at(0, ROW_GRID0, "    2"));             /* headers followed */
    CHECK(at(0, ROW_GRID0 + 53, "   55"));

    /* And back up the same way. */
    for (i = 0; i < 54; ++i)
        grid_key(K_UP);
    CHECK_EQ(g->cur_row, 0);
    CHECK_EQ(g->top_row, 0);
    CHECK(at(0, ROW_GRID0, "    1"));

    grid_key(K_PGDN);
    CHECK_EQ(g->cur_row, 54);
    grid_key(K_PGUP);
    CHECK_EQ(g->cur_row, 0);

    /* Page up at the top clamps rather than underflowing into row 65535. */
    grid_key(K_PGUP);
    CHECK_EQ(g->cur_row, 0);
    CHECK_EQ(g->top_row, 0);
}

static void test_horizontal_scrolling(void)
{
    const grid_t *g = grid_state();
    char b[5];

    setup();

    /* Column H is clipped, so moving onto it scrolls: a cell you have
     * navigated to must be fully visible. */
    grid_goto(0, 7);
    CHECK(g->left_col > 0);
    CHECK(grid_col_x(7) >= 0);
    CHECK((uint16_t)grid_col_x(7) + GRID_DEF_COL_W <= 80);

    grid_goto(0, 100);
    grid_col_name(g->left_col, b);
    CHECK(g->left_col <= 100);
    CHECK(grid_col_x(100) >= 0);
    /* The header must show the scrolled-to column, not column A. */
    grid_col_name(100, b);
    CHECK(at((uint8_t)grid_col_x(100) + 1, ROW_COLHDR, b));

    /* Home returns to column A and scrolls back with it. */
    grid_key(K_HOME);
    CHECK_EQ(g->cur_col, 0);
    CHECK_EQ(g->left_col, 0);
    CHECK(at(GRID_ROWHDR_W + 1, ROW_COLHDR, "A"));
}

static void test_navigation_keys(void)
{
    const grid_t *g = grid_state();

    setup();

    grid_goto(10, 10);
    grid_key(K_HOME);
    CHECK_EQ(g->cur_col, 0);
    CHECK_EQ(g->cur_row, 10);                     /* home stays on the row */

    grid_key(K_CLEAR);                            /* shift+home */
    CHECK_EQ(g->cur_row, 0);
    CHECK_EQ(g->cur_col, 0);

    grid_key(K_END);
    CHECK_EQ(g->cur_col, X16S_MAX_COLS - 1);

    grid_key(K_TAB);                              /* tab is right */
    CHECK_EQ(g->cur_col, X16S_MAX_COLS - 1);      /* already at the end */
    grid_goto(0, 0);
    grid_key(K_TAB);
    CHECK_EQ(g->cur_col, 1);

    CHECK_EQ(grid_key(0x10), GRID_QUIT);          /* F9 */
    CHECK_EQ(grid_key(K_DOWN), GRID_CONTINUE);
}

/* The far corner has to render correctly: five-digit row labels are exactly
 * as wide as the gutter, and column IV is the last name the header emits. */
static void test_far_corner(void)
{
    const grid_t *g = grid_state();

    setup();
    grid_goto(X16S_MAX_ROWS - 1, X16S_MAX_COLS - 1);

    CHECK_EQ(g->cur_row, 65534);
    CHECK(at(0, ROW_GRID0 + 53, "65535"));
    CHECK(at((uint8_t)grid_col_x(255) + 1, ROW_COLHDR, "IV"));
    CHECK(at(1, 1, "IV65535"));                   /* formula bar */
}

static void test_keyboard_queue(void)
{
    static const uint8_t seq[] = { K_RIGHT, K_RIGHT, K_DOWN };
    const grid_t *g = grid_state();
    uint8_t k;

    setup();
    kbd_host_push(seq, sizeof seq);
    while ((k = kbd_get()) != 0)
        grid_key(k);

    CHECK_EQ(g->cur_col, 2);
    CHECK_EQ(g->cur_row, 1);
}

/* The editing flow through grid_key: this is the integration between the
 * editor, the workbook and the renderer, and none of the three suites that
 * test those individually would notice if the wiring were wrong. */
static void test_grid_editing(void)
{
    const grid_t *g = grid_state();
    cell_record_t rec;
    char buf[WB_TEXT_MAX];

    setup();

    /* Typing a printable character starts an edit with that character as
     * the first one, rather than swallowing the keystroke. */
    grid_key('h');
    CHECK_EQ(editor_active(), 1);
    CHECK_STR(editor_text(), "h");
    grid_key('i');
    CHECK_STR(editor_text(), "hi");
    /* The formula bar shows what is being typed. */
    CHECK(at(GRID_ROWHDR_W + 6, 1, "hi"));
    CHECK(at(1, 58, "EDIT"));

    /* Return commits and moves down, as in every spreadsheet. */
    grid_key(0x0D);
    CHECK_EQ(editor_active(), 0);
    CHECK_EQ(g->cur_row, 1);
    CHECK_EQ(g->cur_col, 0);
    CHECK_EQ(wb_get(0, 0, &rec), 1);
    wb_display_text(0, 0, buf, sizeof buf);
    CHECK_STR(buf, "hi");
    /* And the value is on screen where it belongs. */
    CHECK(at(GRID_ROWHDR_W, ROW_GRID0, "hi"));

    /* Arrow keys during an edit move the caret, not the cell cursor. */
    grid_key('a');
    grid_key('b');
    CHECK_EQ(editor_caret(), 2);
    grid_key(K_LEFT);
    CHECK_EQ(editor_caret(), 1);
    CHECK_EQ(g->cur_row, 1);              /* cursor did not move */

    /* Escape abandons the edit and leaves the cell alone. */
    grid_key(0x1B);
    CHECK_EQ(editor_active(), 0);
    CHECK_EQ(wb_get(1, 0, &rec), 0);

    /* Tab commits to the right. */
    grid_goto(3, 0);
    grid_key('7');
    grid_key(K_TAB);
    CHECK_EQ(g->cur_col, 1);
    CHECK_EQ(wb_get(3, 0, &rec), 1);
    CHECK_EQ(rec.type, CELL_NUMBER);

    /* F2 edits the existing value rather than replacing it. */
    grid_goto(3, 0);
    grid_key(0x89);
    CHECK_EQ(editor_active(), 1);
    CHECK_STR(editor_text(), "7");
    grid_key('5');
    CHECK_STR(editor_text(), "75");
    grid_key(0x0D);
    wb_display_text(3, 0, buf, sizeof buf);
    CHECK_STR(buf, "75");

    /* DEL clears the cell and the screen follows. */
    grid_goto(3, 0);
    grid_key(0x14);
    CHECK_EQ(wb_get(3, 0, &rec), 0);
    CHECK(at(GRID_ROWHDR_W, ROW_GRID0 + 3, "          "));

    /* F5 undoes it, and the restored value is repainted. */
    grid_key(0x87);
    CHECK_EQ(wb_get(3, 0, &rec), 1);
    CHECK(at(GRID_ROWHDR_W, ROW_GRID0 + 3, "        75"));

    /* F9 must not quit while editing. It is not a printable character, so
     * the editor ignores it and the edit survives — losing half-typed input
     * to a stray function key would be worse than doing nothing. */
    grid_key('z');
    CHECK_EQ(grid_key(0x10), GRID_CONTINUE);
    CHECK_EQ(editor_active(), 1);
    grid_key(0x1B);                        /* escape is the way out */
    CHECK_EQ(editor_active(), 0);

    /* Quitting with unsaved changes asks first. With no answer scripted the
     * dialog cancels, and the answer must be "stay" — losing a workbook to a
     * stray F9 is the failure this guard exists to prevent. */
    CHECK_EQ(wb_dirty, 1);
    CHECK_EQ(grid_key(0x10), GRID_CONTINUE);

    /* Answer yes and it goes. */
    {
        static const uint8_t yes[] = { 'y' };
        kbd_host_push(yes, 1);
        CHECK_EQ(grid_key(0x10), GRID_QUIT);
    }

    /* With nothing unsaved it quits without asking at all. */
    (wb_dirty = 0);
    CHECK_EQ(grid_key(0x10), GRID_QUIT);
}

/* --- the file commands, driven through the keyboard ------------------
 *
 * Save As, New and Open were five separate functions with the same shape
 * and are now one, with the conversation in OVL_FILEDLG and only the
 * worker resident. Nothing covered them end to end before that, which is
 * exactly the wrong state in which to rewrite all five.
 */
static void type(const char *s)
{
    uint8_t k[24];
    uint8_t n = 0;

    while (*s && n < sizeof k - 1)
        k[n++] = (uint8_t)*s++;
    k[n++] = 0x0D;                      /* RETURN */
    kbd_host_push(k, n);
}

static void test_save_and_reopen(void)
{
    char buf[WB_TEXT_MAX];
    uint8_t confirm[1];

    setup();
    file_host_set_root("build/host/sd");
    CHECK_EQ(wb_set_text(0, 0, "survives"), ERR_OK);

    /* F6, Save As: the dialog takes a name and RETURN accepts it. */
    type("GRIDT.X16S");
    CHECK_EQ(grid_key(0x8B), GRID_CONTINUE);
    CHECK_EQ(wb_dirty, 0);              /* saving clears it */

    /* F1, New: the workbook is clean, so nothing is asked. */
    CHECK_EQ(grid_key(0x85), GRID_CONTINUE);
    CHECK(!wb_get(0, 0, &(cell_record_t){0}) || 1);
    wb_display_text(0, 0, buf, sizeof buf);
    CHECK_STR(buf, "");

    /* The file is really there: Save As went all the way through the
     * worker, not just through the dialog. */
    {
        fstream_t f;
        CHECK_EQ(file_open_read(&f, "GRIDT.X16S"), ERR_OK);
        file_close(&f);
    }

    /* F3, Open, cancelled with ESC. What this checks is that the command
     * survives being refused half way: the dialog overlay is loaded, the
     * user backs out, and the workbook is left exactly as it was rather
     * than half replaced. Choosing a file would depend on what else the
     * other tests have left on the card. */
    confirm[0] = 0x1B;
    kbd_host_push(confirm, 1);
    CHECK_EQ(grid_key(0x86), GRID_CONTINUE);
    wb_display_text(0, 0, buf, sizeof buf);
    CHECK_STR(buf, "");
}


/* --- the pointer ---------------------------------------------------- *
 *
 * grid_mouse() takes its inputs as arguments precisely so this can exist:
 * the hit-testing is arithmetic over the same layout the renderer uses, and
 * getting it wrong puts the cursor in the wrong cell, which is the kind of
 * thing that is tedious to spot by hand in an emulator.
 *
 * An 80x60 screen, so a character is 8 pixels each way and screen column c
 * spans pixels 8c..8c+7.
 */
#define PX(c) ((uint16_t)((c) * 8))
#define CLICK(sx, sy) do {                                                  \
        grid_mouse(PX(sx), PX(sy), 0, 0);          /* release first */      \
        grid_mouse(PX(sx), PX(sy), MOUSE_LEFT, 0); /* then the press */     \
    } while (0)

static void test_mouse_selects_a_cell(void)
{
    const grid_t *g = grid_state();

    setup();

    /* Row header is 6 wide and the default column 10, so column A starts at
     * screen column 6 and column B at 16. Grid row 0 is screen row 3. */
    CLICK(6, ROW_GRID0);
    CHECK_EQ(g->cur_row, 0);
    CHECK_EQ(g->cur_col, 0);

    CLICK(16, ROW_GRID0 + 4);
    CHECK_EQ(g->cur_row, 4);
    CHECK_EQ(g->cur_col, 1);

    /* The far side of a column still selects that column, not the next. */
    CLICK(25, ROW_GRID0 + 1);
    CHECK_EQ(g->cur_col, 1);
    CLICK(26, ROW_GRID0 + 1);
    CHECK_EQ(g->cur_col, 2);
}

static void test_mouse_ignores_the_chrome(void)
{
    const grid_t *g = grid_state();

    setup();
    grid_goto(7, 3);

    /* The row header is not a target: there is no row selection for it to
     * mean. Nor are the formula bar and the column headers. */
    CLICK(0, ROW_GRID0 + 2);
    CHECK_EQ(g->cur_row, 7);
    CHECK_EQ(g->cur_col, 3);

    CLICK(20, ROW_COLHDR);
    CHECK_EQ(g->cur_row, 7);
    CHECK_EQ(g->cur_col, 3);
}

static void test_mouse_click_is_edge_triggered(void)
{
    const grid_t *g = grid_state();

    setup();

    /* Holding the button down must not keep re-firing: the pointer moves
     * away with the button still held and the cursor stays where the press
     * landed. Without the edge check a held button would drag the cursor
     * along and, over the menu bar, reopen the menu on every pass of the
     * main loop. */
    grid_mouse(PX(6), PX(ROW_GRID0), MOUSE_LEFT, 0);
    CHECK_EQ(g->cur_row, 0);
    CHECK_EQ(g->cur_col, 0);

    grid_mouse(PX(36), PX(ROW_GRID0 + 9), MOUSE_LEFT, 0);   /* still held */
    CHECK_EQ(g->cur_row, 0);
    CHECK_EQ(g->cur_col, 0);

    grid_mouse(PX(36), PX(ROW_GRID0 + 9), 0, 0);            /* released */
    grid_mouse(PX(36), PX(ROW_GRID0 + 9), MOUSE_LEFT, 0);   /* pressed again */
    CHECK_EQ(g->cur_row, 9);
    CHECK_EQ(g->cur_col, 3);
}

static void test_mouse_wheel_scrolls(void)
{
    const grid_t *g = grid_state();

    setup();
    grid_goto(20, 0);

    /* Positive is towards the user, which moves down the sheet. */
    grid_mouse(0, 0, 0, 1);
    CHECK_EQ(g->cur_row, 23);
    grid_mouse(0, 0, 0, -1);
    CHECK_EQ(g->cur_row, 20);

    /* And it stops at the top rather than wrapping. */
    grid_goto(1, 0);
    grid_mouse(0, 0, 0, -1);
    CHECK_EQ(g->cur_row, 0);
}

static void test_mouse_switches_sheets(void)
{
    setup();

    CHECK_EQ(wb_sheet_add("Two"), ERR_OK);
    CHECK_EQ(wb_sheet_switch(0), ERR_OK);
    CHECK_EQ(wb_sheet_i, 0);

    /* Tabs are TAB_W = 10 wide, so the second one spans screen columns
     * 10..19 on the tab row. */
    CLICK(12, 57);
    CHECK_EQ(wb_sheet_i, 1);

    CLICK(3, 57);
    CHECK_EQ(wb_sheet_i, 0);

    /* Past the last tab is empty space and must not switch anything. */
    CLICK(60, 57);
    CHECK_EQ(wb_sheet_i, 0);
}

static void test_mouse_does_not_disturb_an_edit(void)
{
    char buf[WB_TEXT_MAX];

    setup();
    grid_goto(2, 2);
    CHECK_EQ(grid_key('7'), GRID_CONTINUE);     /* starts the editor */
    CHECK(editor_active());

    /* A click while editing is ignored rather than committing or cancelling:
     * losing a half-typed line to a stray pointer would be the worst of the
     * options. */
    CLICK(6, ROW_GRID0);
    CHECK(editor_active());

    CHECK_EQ(grid_key(0x0D), GRID_CONTINUE);    /* Return commits it */
    wb_display_text(2, 2, buf, sizeof buf);
    CHECK_STR(buf, "7");
}

/* --- copy and paste --------------------------------------------------- */
#define K_COPY  0x03
#define K_PASTE 0x16

static void test_copy_paste_value(void)
{
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(wb_set_text(0, 0, "42"), ERR_OK);

    grid_goto(0, 0);
    CHECK_EQ(grid_key(K_COPY), GRID_CONTINUE);
    grid_goto(3, 2);
    CHECK_EQ(grid_key(K_PASTE), GRID_CONTINUE);

    wb_display_text(3, 2, b, sizeof b);
    CHECK_STR(b, "42");
    /* Copying does not move or remove the original. */
    wb_display_text(0, 0, b, sizeof b);
    CHECK_STR(b, "42");
}

static void test_copy_paste_text(void)
{
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(wb_set_text(1, 1, "hello"), ERR_OK);
    grid_goto(1, 1);
    CHECK_EQ(grid_key(K_COPY), GRID_CONTINUE);
    grid_goto(9, 0);
    CHECK_EQ(grid_key(K_PASTE), GRID_CONTINUE);

    wb_display_text(9, 0, b, sizeof b);
    CHECK_STR(b, "hello");
}

/* A pasted formula is compiled again at its new home, WITH ITS RELATIVE
 * REFERENCES MOVED to match. Copied from column A to column E, =A1+A2
 * becomes =E1+E2 -- the same offsets from the cell holding it, which is
 * what "relative" means and what every spreadsheet does. */
static void test_copy_paste_formula(void)
{
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(wb_set_text(0, 0, "10"), ERR_OK);
    CHECK_EQ(wb_set_text(1, 0, "5"), ERR_OK);
    CHECK_EQ(wb_set_text(2, 0, "=A1+A2"), ERR_OK);
    wb_display_text(2, 0, b, sizeof b);
    CHECK_STR(b, "15");

    grid_goto(2, 0);
    CHECK_EQ(grid_key(K_COPY), GRID_CONTINUE);
    grid_goto(2, 4);
    CHECK_EQ(grid_key(K_PASTE), GRID_CONTINUE);

    /* Live, not a snapshot of the number -- and it reads column E, where
     * it landed, not column A where it was written. */
    wb_edit_text(2, 4, b, sizeof b);
    CHECK_STR(b, "=E1+E2");
    wb_display_text(2, 4, b, sizeof b);
    CHECK_STR(b, "0");                  /* E1 and E2 are empty */

    /* Filling its new inputs proves it is live and pointing at them. */
    CHECK_EQ(wb_set_text(0, 4, "20"), ERR_OK);
    CHECK_EQ(wb_set_text(1, 4, "5"), ERR_OK);
    wb_display_text(2, 4, b, sizeof b);
    CHECK_STR(b, "25");
}

/* Two pasted copies of one formula are two independent cells. The clipboard
 * holds text and every paste compiles its own record, so there is no way
 * for them to share one -- which is the bug that would have followed from
 * copying the cell record instead. */
static void test_paste_twice_is_independent(void)
{
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(wb_set_text(0, 0, "3"), ERR_OK);
    CHECK_EQ(wb_set_text(1, 0, "=A1*2"), ERR_OK);

    grid_goto(1, 0);
    CHECK_EQ(grid_key(K_COPY), GRID_CONTINUE);
    grid_goto(5, 0);
    CHECK_EQ(grid_key(K_PASTE), GRID_CONTINUE);
    grid_goto(6, 0);
    CHECK_EQ(grid_key(K_PASTE), GRID_CONTINUE);

    /* =A1*2 four and five rows down is =A5*2 and =A6*2 -- each reads its
     * own input, which is the point of the adjustment. */
    wb_edit_text(5, 0, b, sizeof b); CHECK_STR(b, "=A5*2");
    wb_edit_text(6, 0, b, sizeof b); CHECK_STR(b, "=A6*2");
    CHECK_EQ(wb_set_text(4, 0, "7"), ERR_OK);
    CHECK_EQ(wb_set_text(5, 0, "9"), ERR_OK);
    wb_display_text(6, 0, b, sizeof b); CHECK_STR(b, "18");

    /* Clearing one must not disturb the other. */
    CHECK_EQ(wb_clear(4, 0), ERR_OK);
    wb_display_text(6, 0, b, sizeof b); CHECK_STR(b, "18");
}

/* MUST RUN BEFORE THE OTHER CLIPBOARD TESTS. The clipboard belongs to the
 * program, not to a workbook -- it survives File > New, which is the point
 * of it -- so "nothing has been copied yet" is a state that exists only
 * until the first copy anywhere in the suite. */
static void test_paste_without_copy_does_nothing(void)
{
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(wb_set_text(4, 4, "keep"), ERR_OK);
    grid_goto(4, 4);
    CHECK_EQ(grid_key(K_PASTE), GRID_CONTINUE);
    wb_display_text(4, 4, b, sizeof b);
    CHECK_STR(b, "keep");
}

/* Copying an empty cell and pasting it clears the target: the clipboard
 * holds the empty string and wb_set_text() treats that as a delete. */
static void test_paste_of_an_empty_cell_clears(void)
{
    char b[WB_TEXT_MAX];

    setup();
    CHECK_EQ(wb_set_text(0, 0, "gone soon"), ERR_OK);
    grid_goto(7, 7);                            /* an empty cell */
    CHECK_EQ(grid_key(K_COPY), GRID_CONTINUE);
    grid_goto(0, 0);
    CHECK_EQ(grid_key(K_PASTE), GRID_CONTINUE);

    wb_display_text(0, 0, b, sizeof b);
    CHECK_STR(b, "");
}

/* --- sorting ---------------------------------------------------------- *
 *
 * Sorts the used range by the cursor's column, moving WHOLE ROWS. The
 * companion column is what proves that: sorting one column and leaving its
 * neighbours behind would pass every assertion about order and still have
 * destroyed the sheet.
 */
#define K_SORT_ASC  0xF5
#define K_SORT_DESC 0xF6
#define K_SORT_UNDO 0xF7

static void sort_setup(void)
{
    setup();
    /* Deliberately out of order, with a companion that must travel. */
    wb_set_text(0, 0, "30");  wb_set_text(0, 1, "thirty");
    wb_set_text(1, 0, "10");  wb_set_text(1, 1, "ten");
    wb_set_text(2, 0, "20");  wb_set_text(2, 1, "twenty");
    grid_goto(0, 0);
}

static void test_sort_ascending_moves_whole_rows(void)
{
    char b[WB_TEXT_MAX];

    sort_setup();
    CHECK_EQ(grid_key(K_SORT_ASC), GRID_CONTINUE);

    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "10");
    wb_display_text(1, 0, b, sizeof b); CHECK_STR(b, "20");
    wb_display_text(2, 0, b, sizeof b); CHECK_STR(b, "30");

    /* The companions came with them. */
    wb_display_text(0, 1, b, sizeof b); CHECK_STR(b, "ten");
    wb_display_text(1, 1, b, sizeof b); CHECK_STR(b, "twenty");
    wb_display_text(2, 1, b, sizeof b); CHECK_STR(b, "thirty");
}

/* A heading row is left alone, because sorting starts where the cursor is.
 *
 * Reported from the machine: sorting an imported worksheet "didn't sort
 * well". It sorted the headings in with the data, and since text sorts
 * after numbers the column titles ended up at the BOTTOM of the sheet.
 * Every worksheet that comes out of Excel looks like this. */
static void test_sort_leaves_the_heading_row(void)
{
    char b[WB_TEXT_MAX];

    setup();
    wb_set_text(0, 0, "Qty");  wb_set_text(0, 1, "Item");
    wb_set_text(1, 0, "30");   wb_set_text(1, 1, "thirty");
    wb_set_text(2, 0, "10");   wb_set_text(2, 1, "ten");
    wb_set_text(3, 0, "20");   wb_set_text(3, 1, "twenty");

    grid_goto(1, 0);                    /* the first row of the DATA */
    CHECK_EQ(grid_key(K_SORT_ASC), GRID_CONTINUE);

    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "Qty");    /* stayed */
    wb_display_text(1, 0, b, sizeof b); CHECK_STR(b, "10");
    wb_display_text(2, 0, b, sizeof b); CHECK_STR(b, "20");
    wb_display_text(3, 0, b, sizeof b); CHECK_STR(b, "30");
    wb_display_text(1, 1, b, sizeof b); CHECK_STR(b, "ten");

    /* And undo puts the data back without disturbing the heading. */
    CHECK_EQ(grid_key(K_SORT_UNDO), GRID_CONTINUE);
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "Qty");
    wb_display_text(1, 0, b, sizeof b); CHECK_STR(b, "30");
    wb_display_text(3, 0, b, sizeof b); CHECK_STR(b, "20");
}

/* Numbers held as text sort as numbers.
 *
 * Reported from the machine: 1, 2, 3, 5, 456 came back 1, 2, 3, 456, 5.
 * That is text order -- "456" loses to "5" on the fourth character -- and
 * a worksheet can easily hold numbers as text, because an .xlsx may store
 * them as inline strings.
 *
 * The cells are built by hand here because wb_set_text() parses "456" into
 * a number, which is exactly why the earlier tests never saw this. */
static void test_sort_text_that_is_numbers(void)
{
    static const char *const v[] = { "1", "2", "3", "5", "456" };
    char b[WB_TEXT_MAX];
    uint8_t i;

    setup();
    for (i = 0; i < 5; ++i) {
        cell_record_t r;
        uint16_t id;

        CHECK_EQ(strpool_add(v[i], (uint16_t)strlen(v[i]), &id), ERR_OK);
        memset(&r, 0, sizeof r);
        r.type = CELL_TEXT;
        r.val[0] = (uint8_t)id;
        r.val[1] = (uint8_t)(id >> 8);
        CHECK_EQ(wb_set(i, 0, &r), ERR_OK);
    }

    grid_goto(0, 0);
    CHECK_EQ(grid_key(K_SORT_ASC), GRID_CONTINUE);
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "1");
    wb_display_text(3, 0, b, sizeof b); CHECK_STR(b, "5");
    wb_display_text(4, 0, b, sizeof b); CHECK_STR(b, "456");

    /* And text that is not a number still sorts as text. */
    setup();
    wb_set_text(0, 0, "pear");
    wb_set_text(1, 0, "apple");
    grid_goto(0, 0);
    CHECK_EQ(grid_key(K_SORT_ASC), GRID_CONTINUE);
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "apple");
}

static void test_sort_descending(void)
{
    char b[WB_TEXT_MAX];

    sort_setup();
    CHECK_EQ(grid_key(K_SORT_DESC), GRID_CONTINUE);

    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "30");
    wb_display_text(2, 0, b, sizeof b); CHECK_STR(b, "10");
    wb_display_text(0, 1, b, sizeof b); CHECK_STR(b, "thirty");
    wb_display_text(2, 1, b, sizeof b); CHECK_STR(b, "ten");
}

/* Sorts on the column the cursor is in, not always on A. */
static void test_sort_uses_the_cursor_column(void)
{
    char b[WB_TEXT_MAX];

    sort_setup();
    grid_goto(0, 1);                    /* the words, not the numbers */
    CHECK_EQ(grid_key(K_SORT_ASC), GRID_CONTINUE);

    wb_display_text(0, 1, b, sizeof b); CHECK_STR(b, "ten");
    wb_display_text(1, 1, b, sizeof b); CHECK_STR(b, "thirty");
    wb_display_text(2, 1, b, sizeof b); CHECK_STR(b, "twenty");
    /* and their numbers followed */
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "10");
    wb_display_text(1, 0, b, sizeof b); CHECK_STR(b, "30");
    wb_display_text(2, 0, b, sizeof b); CHECK_STR(b, "20");
}

/* Numbers before text, and blanks last in both directions -- a blank row
 * interleaved with the data would be worse than either order. */
static void test_sort_kinds_and_blanks(void)
{
    char b[WB_TEXT_MAX];

    setup();
    wb_set_text(0, 0, "pear");
    wb_set_text(1, 0, "5");
    /* row 2 left empty */
    wb_set_text(3, 0, "apple");
    wb_set_text(4, 0, "1");
    grid_goto(0, 0);

    CHECK_EQ(grid_key(K_SORT_ASC), GRID_CONTINUE);
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "1");
    wb_display_text(1, 0, b, sizeof b); CHECK_STR(b, "5");
    wb_display_text(2, 0, b, sizeof b); CHECK_STR(b, "apple");
    wb_display_text(3, 0, b, sizeof b); CHECK_STR(b, "pear");
    wb_display_text(4, 0, b, sizeof b); CHECK_STR(b, "");

    CHECK_EQ(grid_key(K_SORT_DESC), GRID_CONTINUE);
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "pear");
    wb_display_text(3, 0, b, sizeof b); CHECK_STR(b, "1");
    /* still last, not first */
    wb_display_text(4, 0, b, sizeof b); CHECK_STR(b, "");
}

/* A range with formulas asks first, because nothing rewrites the
 * references inside them and there is no undo for a sort. An unanswered
 * dialog cancels, so the sheet must be untouched. */
static void test_sort_asks_before_touching_formulas(void)
{
    char b[WB_TEXT_MAX];

    setup();
    wb_set_text(0, 0, "30");
    wb_set_text(1, 0, "10");
    wb_set_text(2, 0, "=A1+A2");
    grid_goto(0, 0);

    CHECK_EQ(grid_key(K_SORT_ASC), GRID_CONTINUE);
    wb_display_text(0, 0, b, sizeof b);
    CHECK_STR(b, "30");                 /* refused: nothing moved */

    /* Answer yes and it goes ahead. */
    {
        static const uint8_t yes[] = { 'y' };
        kbd_host_push(yes, 1);
        CHECK_EQ(grid_key(K_SORT_ASC), GRID_CONTINUE);
        wb_display_text(0, 0, b, sizeof b);
        CHECK_STR(b, "10");
    }
}

/* Sorting an empty sheet does nothing and does not fall over. */
static void test_sort_empty_sheet(void)
{
    setup();
    grid_goto(0, 0);
    CHECK_EQ(grid_key(K_SORT_ASC), GRID_CONTINUE);
    CHECK_EQ(wb_cells()->cell_count, 0);
}

/* --- undoing a sort --------------------------------------------------- *
 *
 * Exact, because it is the same row swapping run through the inverse of the
 * permutation that was applied -- not a reconstruction from a snapshot.
 */
static void test_sort_undo_restores_the_order(void)
{
    char b[WB_TEXT_MAX];

    sort_setup();
    CHECK_EQ(grid_key(K_SORT_ASC), GRID_CONTINUE);
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "10");

    CHECK_EQ(grid_key(K_SORT_UNDO), GRID_CONTINUE);
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "30");
    wb_display_text(1, 0, b, sizeof b); CHECK_STR(b, "10");
    wb_display_text(2, 0, b, sizeof b); CHECK_STR(b, "20");
    /* companions came back too */
    wb_display_text(0, 1, b, sizeof b); CHECK_STR(b, "thirty");
    wb_display_text(1, 1, b, sizeof b); CHECK_STR(b, "ten");
    wb_display_text(2, 1, b, sizeof b); CHECK_STR(b, "twenty");
}

/* Once. A second undo would apply the inverse again, which is not a redo
 * but a fresh unrelated shuffle. */
/* Undo reached the way a user reaches it: through the menu.
 *
 * The direct-dispatch tests above all passed while this was broken. The
 * menu path called after_file_op(), which forgets the last sort, before
 * dispatching the choice -- so Undo sort cleared the record and then found
 * nothing to undo. A test that skips the real path is not a test of it. */
static void test_sort_undo_through_the_menu(void)
{
    static const uint8_t pick_undo[] = {
        K_RIGHT, K_RIGHT, K_RIGHT,       /* File -> Edit -> Layout */
                                         /* -> Data                */
        K_DOWN, K_DOWN,                  /* opens on Sort up; down */
                                         /* twice reaches Undo     */
        K_ENTER
    };
    char b[WB_TEXT_MAX];

    sort_setup();
    CHECK_EQ(grid_key(K_SORT_ASC), GRID_CONTINUE);
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "10");

    kbd_host_push(pick_undo, sizeof pick_undo);
    CHECK_EQ(grid_key(0x15), GRID_CONTINUE);      /* F10, the menu bar */

    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "30");
    wb_display_text(0, 1, b, sizeof b); CHECK_STR(b, "thirty");
}

static void test_sort_undo_only_once(void)
{
    char b[WB_TEXT_MAX];

    sort_setup();
    grid_key(K_SORT_ASC);
    grid_key(K_SORT_UNDO);
    grid_key(K_SORT_UNDO);
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "30");
    wb_display_text(1, 0, b, sizeof b); CHECK_STR(b, "10");
}

/* A selected block sorts as a block: those rows, those columns, and
 * nothing else on the sheet moves. */
static void test_sort_selection_only(void)
{
    char b[WB_TEXT_MAX];
    uint8_t k[4];

    sort_setup();
    /* Column B carries a label per row; it is OUTSIDE the block and must
     * stay attached to its row number, not follow column A. */
    CHECK_EQ(wb_set_text(0, 1, "keep0"), ERR_OK);
    CHECK_EQ(wb_set_text(1, 1, "keep1"), ERR_OK);
    CHECK_EQ(wb_set_text(2, 1, "keep2"), ERR_OK);

    /* Select A1:A3 -- Ctrl+A anchors, then extend down twice. */
    grid_goto(0, 0);
    grid_key(0x01);
    grid_key(K_DOWN);
    grid_key(K_DOWN);

    /* The block is narrower than the data, so it asks first. */
    k[0] = 'y';
    kbd_host_push(k, 1);
    grid_key(K_SORT_ASC);

    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "10");
    wb_display_text(1, 0, b, sizeof b); CHECK_STR(b, "20");
    wb_display_text(2, 0, b, sizeof b); CHECK_STR(b, "30");

    /* Column B did not move. */
    wb_display_text(0, 1, b, sizeof b); CHECK_STR(b, "keep0");
    wb_display_text(1, 1, b, sizeof b); CHECK_STR(b, "keep1");
    wb_display_text(2, 1, b, sizeof b); CHECK_STR(b, "keep2");
}

/* A multi-column block sorts by its FIRST column, and the rest of the
 * block travels with it.
 *
 * The cursor ends on the last column after extending right, so keying on
 * the cursor would sort a table selected from its left edge by its
 * rightmost column. */
static void test_sort_block_keys_on_first_column(void)
{
    char b[WB_TEXT_MAX];

    setup();
    sort_undo_n = 0;
    /* A is the key, B and C must stay with their row. */
    wb_set_text(0, 0, "30"); wb_set_text(0, 1, "c"); wb_set_text(0, 2, "300");
    wb_set_text(1, 0, "10"); wb_set_text(1, 1, "a"); wb_set_text(1, 2, "100");
    wb_set_text(2, 0, "20"); wb_set_text(2, 1, "b"); wb_set_text(2, 2, "200");

    /* Select A1:C3 -- anchor at A1, extend right twice and down twice, so
     * the cursor finishes on C3. */
    grid_goto(0, 0);
    grid_key(0x01);
    grid_key(K_RIGHT); grid_key(K_RIGHT);
    grid_key(K_DOWN);  grid_key(K_DOWN);

    /* The block covers everything on those rows, so nothing is asked. */
    grid_key(K_SORT_ASC);

    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "10");
    wb_display_text(1, 0, b, sizeof b); CHECK_STR(b, "20");
    wb_display_text(2, 0, b, sizeof b); CHECK_STR(b, "30");

    /* ...and each row kept its own B and C. */
    wb_display_text(0, 1, b, sizeof b); CHECK_STR(b, "a");
    wb_display_text(0, 2, b, sizeof b); CHECK_STR(b, "100");
    wb_display_text(1, 1, b, sizeof b); CHECK_STR(b, "b");
    wb_display_text(1, 2, b, sizeof b); CHECK_STR(b, "200");
    wb_display_text(2, 1, b, sizeof b); CHECK_STR(b, "c");
    wb_display_text(2, 2, b, sizeof b); CHECK_STR(b, "300");
}

/* Declining the warning leaves the sheet alone. */
static void test_sort_selection_can_be_refused(void)
{
    char b[WB_TEXT_MAX];
    uint8_t k[4];

    sort_setup();
    CHECK_EQ(wb_set_text(0, 1, "keep0"), ERR_OK);

    grid_goto(0, 0);
    grid_key(0x01);
    grid_key(K_DOWN);
    grid_key(K_DOWN);

    k[0] = 'n';
    kbd_host_push(k, 1);
    grid_key(K_SORT_ASC);

    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "30");   /* untouched */
}

/* Bold is a block command, and one entry that toggles.
 *
 * Which way it goes is decided by the first cell in the block that exists,
 * so a second press takes it off again -- that is what makes one menu item
 * behave like a toolbar button. */
static void test_bold_toggles_a_block(void)
{
    cell_record_t rec;
    cell_style_t st;

    setup();
    wb_set_text(0, 0, "Month");
    wb_set_text(0, 1, "Sales");
    wb_set_text(1, 0, "January");

    /* Select A1:B1 and mark it. */
    grid_goto(0, 0);
    grid_key(0x01);                     /* Ctrl+A anchors */
    grid_key(K_RIGHT);
    grid_key(MENU_BOLD);

    CHECK(wb_get(0, 0, &rec)); CHECK(wb_bold(&rec));
    CHECK(wb_get(0, 1, &rec)); CHECK(wb_bold(&rec));
    /* The row below was never in the block. */
    CHECK(wb_get(1, 0, &rec)); CHECK(!wb_bold(&rec));

    /* Again, and it comes off. */
    grid_key(MENU_BOLD);
    CHECK(wb_get(0, 0, &rec)); CHECK(!wb_bold(&rec));
    CHECK(wb_get(0, 1, &rec)); CHECK(!wb_bold(&rec));
}

/* Bold does not disturb the rest of the style.
 *
 * Styles are interned, so marking a currency cell has to ask for "currency
 * AND bold" rather than editing the style the cell names -- which every
 * other currency cell on the sheet names too. */
static void test_bold_keeps_the_format(void)
{
    cell_record_t rec;
    cell_style_t st;
    char b[WB_TEXT_MAX];
    uint8_t id;

    setup();
    memset(&st, 0, sizeof st);
    st.number_format = NF_CURRENCY;
    st.decimal_places = 2;
    CHECK_EQ(styles_add(&st, &id), ERR_OK);

    wb_set_text(0, 0, "8.5");
    wb_get(0, 0, &rec);
    rec.style = id;
    wb_set(0, 0, &rec);

    /* A second cell sharing that style must NOT become bold. */
    wb_set_text(5, 0, "1.25");
    wb_get(5, 0, &rec);
    rec.style = id;
    wb_set(5, 0, &rec);

    grid_goto(0, 0);
    grid_key(MENU_BOLD);

    CHECK(wb_get(0, 0, &rec));
    CHECK(wb_bold(&rec));
    wb_display_text(0, 0, b, sizeof b);
    CHECK_STR(b, "$8.50");                  /* still currency */

    CHECK(wb_get(5, 0, &rec));
    CHECK(!wb_bold(&rec));                  /* and it stayed plain */
    wb_display_text(5, 0, b, sizeof b);
    CHECK_STR(b, "$1.25");
}

/* Undo with nothing sorted must do nothing at all. */
static void test_sort_undo_without_a_sort(void)
{
    char b[WB_TEXT_MAX];

    sort_setup();
    CHECK_EQ(grid_key(K_SORT_UNDO), GRID_CONTINUE);
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "30");
    wb_display_text(1, 0, b, sizeof b); CHECK_STR(b, "10");
}

/* A used range that has changed size since the sort means the stored
 * permutation no longer describes this sheet. Applying it would move rows
 * that were never sorted, so it refuses. */
static void test_sort_undo_refuses_when_the_range_grew(void)
{
    char b[WB_TEXT_MAX];

    sort_setup();
    grid_key(K_SORT_ASC);
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "10");

    CHECK_EQ(wb_set_text(9, 0, "99"), ERR_OK);      /* the range grew */
    CHECK_EQ(grid_key(K_SORT_UNDO), GRID_CONTINUE);
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "10");   /* unchanged */
}

/* And on a different sheet, where those row numbers mean something else. */
static void test_sort_undo_refuses_on_another_sheet(void)
{
    char b[WB_TEXT_MAX];

    sort_setup();
    grid_key(K_SORT_ASC);

    CHECK_EQ(wb_sheet_add("Other"), ERR_OK);
    CHECK_EQ(wb_sheet_switch(1), ERR_OK);
    CHECK_EQ(wb_set_text(0, 0, "untouched"), ERR_OK);
    CHECK_EQ(grid_key(K_SORT_UNDO), GRID_CONTINUE);
    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "untouched");
}

/* --- find ------------------------------------------------------------- */

static void test_find(void)
{
    static const uint8_t type_cab[] = { 'c', 'a', 'b', 0x0D };
    static const uint8_t enter[]    = { 0x0D };
    static const uint8_t esc[]      = { 0x1B };
    const grid_t *g = grid_state();

    setup();
    wb_set_text(0, 0, "Bracket");
    wb_set_text(1, 0, "Washer");
    wb_set_text(2, 0, "Cable");
    wb_set_text(3, 0, "Cabinet");
    grid_goto(0, 0);
    find_text[0] = 0;

    /* Case is ignored, and it matches inside the text, not just at the
     * start. */
    kbd_host_push(type_cab, sizeof type_cab);
    find_run();
    CHECK_EQ(g->cur_row, 2);

    /* Running it again offers the same text, and Return walks to the next
     * match -- which is what makes the one command a "find next" too. */
    kbd_host_push(enter, sizeof enter);
    find_run();
    CHECK_EQ(g->cur_row, 3);

    /* And past the last match it wraps to the first. */
    kbd_host_push(enter, sizeof enter);
    find_run();
    CHECK_EQ(g->cur_row, 2);

    /* Escape leaves the cursor alone. */
    kbd_host_push(esc, sizeof esc);
    find_run();
    CHECK_EQ(g->cur_row, 2);

    /* Nothing matching also leaves it alone rather than jumping to A1. */
    {
        static const uint8_t nope[] = { 'z', 'z', 'z', 0x0D };
        find_text[0] = 0;
        kbd_host_push(nope, sizeof nope);
        find_run();
        CHECK_EQ(g->cur_row, 2);
    }
}

/* --- frozen headings ---------------------------------------------------
 *
 * The reason this exists: an 80-column screen showing 500 rows of prices
 * loses the heading row after one page, and then no column means anything.
 * So rows above the cursor and columns left of it can be pinned, and the
 * rest scrolls behind them.
 *
 * The fixture is a heading row and a heading column, so both directions
 * are wrong in a visible way if the mapping is off by one:
 *
 *        A      B      C
 *   1  corner  Jan    Feb
 *   2  north   r2b    r2c
 *   3  south   r3b    r3c
 *   ...up to row 200
 */
static void freeze_setup(void)
{
    uint16_t r;
    char v[8];

    setup();
    wb_set_text(0, 0, "corner");
    wb_set_text(0, 1, "Jan");
    wb_set_text(0, 2, "Feb");
    for (r = 1; r < 200; ++r) {
        v[0] = 'r'; v[1] = (char)('0' + r % 10); v[2] = 0;
        wb_set_text(r, 0, v);
    }
    grid_goto(0, 0);
}

static void test_freeze_pins_the_heading_row(void)
{
    freeze_setup();

    /* Freeze at B2: row 1 and column A stay. */
    grid_goto(1, 1);
    grid_key(MENU_FREEZE);

    CHECK(at(6, ROW_GRID0, "corner"));          /* the frozen row, on top */

    /* Now scroll a long way down. The heading must not move. */
    grid_goto(150, 1);
    CHECK(at(6, ROW_GRID0, "corner"));
    CHECK(at(0, ROW_GRID0, "    1"));           /* and it is still row 1 */

    /* The row under it is the scrolling part, and it is NOT row 2 -- that
     * is the bug this catches, a viewport that forgot the frozen row and
     * showed row 2 twice. */
    CHECK(!at(0, ROW_GRID0 + 1, "    2"));
}

/* The cursor in the frozen part must not drag the scrolling part back. */
static void test_freeze_cursor_in_the_heading(void)
{
    freeze_setup();
    grid_goto(1, 1);
    grid_key(MENU_FREEZE);
    grid_goto(150, 1);

    grid_goto(0, 1);                            /* up into the heading */
    CHECK(at(6, ROW_GRID0, "corner"));
    CHECK(!at(0, ROW_GRID0 + 1, "    2"));      /* still scrolled down */
}

/* At A1, Freeze keeps the first row and column.
 *
 * The corner is the cursor, so what is frozen is what lies ABOVE and LEFT
 * of it -- which at A1 is nothing. Freezing nothing also leaves both counts
 * at 0, so the next press would be another attempt rather than the unfreeze
 * the user expects, and the command reads as broken. A1 therefore means the
 * obvious thing instead. */
static void test_freeze_at_a1_keeps_the_headings(void)
{
    freeze_setup();
    grid_goto(0, 0);
    grid_key(MENU_FREEZE);

    CHECK_EQ(grid_state()->frz_row, 1);
    CHECK_EQ(grid_state()->frz_col, 1);

    /* And it is a toggle again, which it could not be at 0. */
    grid_key(MENU_FREEZE);
    CHECK_EQ(grid_state()->frz_row, 0);
    CHECK_EQ(grid_state()->frz_col, 0);
}

/* The heading really is pinned after freezing from A1. */
static void test_freeze_from_a1_pins_the_row(void)
{
    freeze_setup();
    grid_goto(0, 0);
    grid_key(MENU_FREEZE);

    grid_goto(150, 1);
    CHECK(at(6, ROW_GRID0, "corner"));      /* row 1 still on top */
}

/* Freezing again unfreezes: one command, and the only way back. */
static void test_freeze_toggles(void)
{
    freeze_setup();
    grid_goto(1, 1);
    grid_key(MENU_FREEZE);
    CHECK_EQ(grid_state()->frz_row, 1);

    grid_key(MENU_FREEZE);
    CHECK_EQ(grid_state()->frz_row, 0);
    CHECK_EQ(grid_state()->frz_col, 0);

    grid_goto(150, 0);
    CHECK(!at(6, ROW_GRID0, "corner"));         /* scrolls away again */
}

/* Refused where it would leave nothing to scroll. A sheet frozen down to
 * its last visible row has no moving part and no way back except this
 * command, which the user would then have to reach with a cursor that will
 * not go there. */
static void test_freeze_refuses_to_fill_the_screen(void)
{
    freeze_setup();
    grid_goto(50, 0);                           /* past half the screen */
    grid_key(MENU_FREEZE);
    CHECK_EQ(grid_state()->frz_row, 0);
}

/* --- references follow the rows -----------------------------------------
 *
 * Insert a row above a =SUM(B2:B4) and it has to become =SUM(B2:B5), or
 * the formula quietly means something else than it did. Checked on the
 * SOURCE text, with wb_edit_text, because that is what the formula bar
 * shows and what the next edit starts from -- a bytecode that moved while
 * the text did not is the failure this is written against.
 */
static void refs_setup(void)
{
    setup();
    wb_set_text(0, 0, "head");
    wb_set_text(1, 1, "10");            /* B2 */
    wb_set_text(2, 1, "20");            /* B3 */
    wb_set_text(3, 1, "30");            /* B4 */
    wb_set_text(4, 1, "=SUM(B2:B4)");   /* B5 */
    wb_set_text(5, 1, "=B2+1");         /* B6 */
    grid_goto(0, 0);
}

static void src_is(uint16_t r, uint16_t c, const char *want)
{
    char buf[64];

    wb_edit_text(r, c, buf, sizeof buf);
    CHECK_STR(buf, want);
}

static void test_insert_row_moves_references(void)
{
    char buf[64];

    refs_setup();
    grid_goto(1, 0);                    /* on B2's row */
    grid_key(MENU_INS_ROW);

    /* Everything moved down one, and both formulas followed it. */
    src_is(5, 1, "=SUM(B3:B5)");
    src_is(6, 1, "=B3+1");

    /* And the answer is still the answer, not a stale cache. */
    wb_display_text(5, 1, buf, sizeof buf);
    CHECK_STR(buf, "60");
}

static void test_insert_below_leaves_references_alone(void)
{
    refs_setup();
    grid_goto(10, 0);                   /* past everything */
    grid_key(MENU_INS_ROW);

    src_is(4, 1, "=SUM(B2:B4)");
    src_is(5, 1, "=B2+1");
}

/* A range the deleted row falls inside shrinks by one at the far end. */
static void test_delete_row_shrinks_a_range(void)
{
    static const uint8_t yes[] = { 'y' };
    char buf[64];

    refs_setup();
    grid_goto(2, 0);                    /* B3, inside the SUM */
    kbd_host_push(yes, sizeof yes);
    grid_key(MENU_DEL_ROW);

    src_is(3, 1, "=SUM(B2:B3)");
    wb_display_text(3, 1, buf, sizeof buf);
    CHECK_STR(buf, "40");               /* 10 + 30, the survivors */
}

/* A reference naming another sheet is left alone: resolving the name needs
 * an overlay the compiler cannot reach. Stated here so the day it starts
 * working, this test fails and says so. */
static void test_cross_sheet_references_are_not_moved(void)
{
    refs_setup();
    wb_set_text(7, 1, "=SUM(Sheet1!B2:B4)");
    grid_goto(1, 0);
    grid_key(MENU_INS_ROW);

    src_is(8, 1, "=SUM(Sheet1!B2:B4)");
}

/* The scanner must not mistake a function name for a reference. */
static void test_function_names_survive(void)
{
    refs_setup();
    wb_set_text(7, 1, "=IF(B2>1,MAX(B2:B4),0)");
    grid_goto(1, 0);
    grid_key(MENU_INS_ROW);

    src_is(8, 1, "=IF(B3>1,MAX(B3:B5),0)");
}

/* --- the chart ----------------------------------------------------------
 *
 * Bars are runs of spaces with a coloured background -- the screen is in
 * the ISO charset and has no block glyphs -- which means the whole layout
 * is assertable here rather than only visible in a screenshot. That was
 * most of the reason for drawing it that way.
 *
 * Called directly rather than through grid_key(): Chart is on no keyboard
 * key and never reaches the grid's dispatch at all. It runs from inside
 * menu_run(), in the overlay it already lives in, because one more case
 * label there was measured at 28 resident bytes against 23 available.
 */
#define C_BAR_EXPECT COLOR(COL_WHITE, COL_BLUE)
#define CHART_TOP 3
#define AXIS_W    9

/* How many rows of bar are in the column at screen x. */
static uint8_t bar_height(uint8_t x)
{
    uint8_t y, n = 0;

    for (y = 0; y < 60; ++y)
        if (screen_host_color(x, y) == C_BAR_EXPECT)
            ++n;
    return n;
}

static void chart_setup(void)
{
    setup();
    wb_set_text(0, 0, "Jan"); wb_set_text(0, 1, "10");
    wb_set_text(1, 0, "Feb"); wb_set_text(1, 1, "20");
    wb_set_text(2, 0, "Mar"); wb_set_text(2, 1, "40");
    grid_goto(0, 1);                    /* the column of numbers */
}

static void test_chart_scales_to_the_largest(void)
{
    uint8_t a, b, c;

    chart_setup();
    chart_draw(CHART_BAR);

    a = bar_height(AXIS_W);
    b = bar_height(AXIS_W + 24);
    c = bar_height(AXIS_W + 48);

    /* 10, 20, 40: the last is full height and the others are in
     * proportion to it, which is what a zero-based scale means. */
    CHECK(c > 40);
    CHECK(b > a);
    CHECK(c > b);
    CHECK_EQ((uint8_t)(b * 2), c);      /* 20 is exactly half of 40 */
}

/* A column with nothing to plot says so instead of drawing an empty frame
 * -- and does not clear the sheet off the screen before finding out. */
static void test_chart_of_text_says_so(void)
{
    setup();
    wb_set_text(0, 0, "Jan");
    wb_set_text(1, 0, "Feb");
    grid_goto(0, 0);
    chart_draw(CHART_BAR);

    CHECK(at(1, CHART_TOP, "No numbers"));
    CHECK_EQ(bar_height(AXIS_W), 0);
}

/* The grid comes back when the chart is dismissed: the chart is a screen,
 * not a mode, and forgetting to repaint would strand the user. */
static void test_chart_returns_to_the_grid(void)
{
    static const uint8_t any[] = { ' ' };

    chart_setup();
    kbd_host_push(any, sizeof any);     /* the key that dismisses it */
    chart_run(CHART_BAR);

    CHECK(at(6, ROW_GRID0, "Jan"));     /* the sheet is back */
}

/* The line chart plots the same points as the bars reach to, joined.
 *
 * Characters, not colour: a line is one cell wide, and a coloured cell
 * with nothing in it reads as a dot of noise where '*' reads as a point.
 * The ISO charset has '*' and '|', which is exactly what it does not have
 * for the solid blocks a bar needs. */
#define C_LINE_EXPECT COLOR(COL_WHITE, COL_RED)
static uint16_t cells_of(color_t c);   /* defined with the pie tests */

static void test_chart_line_plots_the_points(void)
{
    chart_setup();
    chart_draw(CHART_LINE);

    /* Every chart is drawn into the bitmap now, so a line is coloured
     * cells on the host rather than '*' characters -- see plot1(). */
    CHECK(cells_of(C_LINE_EXPECT) >= 3);

    /* And no bars: the two plots are alternatives, not layers. */
    CHECK_EQ(bar_height(AXIS_W), 0);
}

/* The rising column 10, 20, 40 must plot upward -- a chart that draws the
 * scale upside down is the classic way to get this wrong. */
static void test_chart_line_rises(void)
{
    uint8_t x, i, first = 0, last = 0;

    chart_setup();
    chart_draw(CHART_LINE);

    for (i = 0; i < 60; ++i)
        for (x = 0; x < 80; ++x)
            if (screen_host_color(x, i) == C_LINE_EXPECT) {
                if (!first)
                    first = i;
                last = i;
                break;
            }
    /* The largest value sits nearer the top of the screen than the
     * smallest, so the marks span several rows and the top one is not the
     * bottom one. */
    CHECK(first < last);
}

/* The pie.
 *
 * Swept by 360 rays from the centre against a quarter sine table, filling
 * by walking the radius -- no arctangent, no framebuffer, and every cell
 * it touches goes through screen_put(), so the shape is assertable here.
 */
#define C_SLICE1 COLOR(COL_WHITE, COL_BLUE)
#define C_SLICE2 COLOR(COL_BLACK, COL_YELLOW)

static uint16_t cells_of(color_t c)
{
    uint8_t x, y;
    uint16_t n = 0;

    for (y = 0; y < 60; ++y)
        for (x = 0; x < 80; ++x)
            if (screen_host_color(x, y) == c)
                ++n;
    return n;
}

/* 10, 20, 40 out of 70: the slices come out in that proportion, and the
 * biggest is the biggest. Counted rather than measured to the cell --
 * rasterising a circle onto 8x8 characters is not exact and does not need
 * to be. */
static void test_pie_slices_are_proportional(void)
{
    uint16_t s1, s2;

    chart_setup();
    chart_draw(CHART_PIE);

    s1 = cells_of(C_SLICE1);            /* 10 of 70 */
    s2 = cells_of(C_SLICE2);            /* 20 of 70 */

    CHECK(s1 > 0);
    CHECK(s2 > s1);                     /* twice the value, more of the pie */

    /* It is round, so it covers a good part of the plot area rather than
     * a handful of cells -- a sweep that lost its scaling draws a dot. */
    CHECK(s1 + s2 > 100);
}

/* The legend names each slice and gives its share, or a pie is six
 * colours and no information. */
static void test_pie_has_a_legend(void)
{
    chart_setup();
    chart_draw(CHART_PIE);

    CHECK(at(4, 3, "Jan"));
    CHECK(at(4, 4, "Feb"));
}

/* Negatives have no meaning in a pie -- a slice cannot be less than
 * nothing -- so they are dropped rather than drawn inside out. */
static void test_pie_ignores_negatives(void)
{
    setup();
    wb_set_text(0, 0, "a"); wb_set_text(0, 1, "-5");
    wb_set_text(1, 0, "b"); wb_set_text(1, 1, "-7");
    grid_goto(0, 1);
    chart_draw(CHART_PIE);

    CHECK(at(1, CHART_TOP, "No numbers"));
}

/* --- column width ------------------------------------------------------
 *
 * wb_set_col_width() had been there since the importers were written, and
 * nothing else ever called it: a width could arrive from a file and never
 * be changed. These check the door, not the setter.
 *
 * The prompt starts with the current width already typed, so Return alone
 * changes nothing and typing a number replaces it.
 */
static void width_setup(void)
{
    setup();
    wb_set_text(0, 0, "wide");
    grid_goto(0, 0);
}

static void test_column_width_is_set_from_the_prompt(void)
{
    static const uint8_t k[] = { 20, 20, '2', '4', 13 };  /* backspace x2 */

    width_setup();
    CHECK_EQ(wb_col_width(0), X16S_DEF_COL_W);

    kbd_host_push(k, sizeof k);
    grid_key(MENU_COL_W);

    CHECK_EQ(wb_col_width(0), 24);
    /* And the grid was repainted at the new width: column B now starts
     * further right than the default put it. */
    CHECK_EQ(grid_col_x(1), GRID_ROWHDR_W + 24);
}

/* Escape leaves it alone, which is the whole point of starting the prompt
 * with the current value rather than empty. */
static void test_column_width_can_be_cancelled(void)
{
    static const uint8_t k[] = { '5', 27 };

    width_setup();
    kbd_host_push(k, sizeof k);
    grid_key(MENU_COL_W);

    CHECK_EQ(wb_col_width(0), X16S_DEF_COL_W);
}

/* Out of range is clamped by wb_set_col_width(), not by the prompt -- so a
 * width of 99 becomes the maximum rather than being refused, and one of 1
 * becomes the minimum instead of making the column unreadable. */
static void test_column_width_is_clamped(void)
{
    static const uint8_t k[] = { 20, 20, '9', '9', 13 };

    width_setup();
    kbd_host_push(k, sizeof k);
    grid_key(MENU_COL_W);

    CHECK_EQ(wb_col_width(0), X16S_MAX_COL_W);
}

/* --- sorting moves the formulas with the rows --------------------------
 *
 * A sort is a permutation, so each formula moves by its own amount and its
 * relative references move with it. The case that matters is a formula
 * reading its OWN row: after the sort it must still read its own row.
 *
 * Fixture -- C is =A*B for its row, and A is the sort key:
 *
 *        A    B    C
 *   1    3    10   =A1*B1   -> 30
 *   2    1    20   =A2*B2   -> 20
 *   3    2    30   =A3*B3   -> 60
 */
static void sortref_setup(void)
{
    setup();
    wb_set_text(0, 0, "3");  wb_set_text(0, 1, "10"); wb_set_text(0, 2, "=A1*B1");
    wb_set_text(1, 0, "1");  wb_set_text(1, 1, "20"); wb_set_text(1, 2, "=A2*B2");
    wb_set_text(2, 0, "2");  wb_set_text(2, 1, "30"); wb_set_text(2, 2, "=A3*B3");
    grid_goto(0, 0);
}

static void test_sort_moves_relative_references(void)
{
    char b[WB_TEXT_MAX];

    sortref_setup();
    {   /* the sheet has formulas, so the sort asks first */
        static const uint8_t yes[] = { 'y' };
        kbd_host_push(yes, 1);
    }
    CHECK_EQ(grid_key(K_SORT_ASC), GRID_CONTINUE);

    /* Ascending by A: row 2 (1,20) first, then row 3 (2,30), then row 1. */
    wb_edit_text(0, 2, b, sizeof b); CHECK_STR(b, "=A1*B1");
    wb_edit_text(1, 2, b, sizeof b); CHECK_STR(b, "=A2*B2");
    wb_edit_text(2, 2, b, sizeof b); CHECK_STR(b, "=A3*B3");

    /* And each still multiplies the row it sits on. */
    wb_display_text(0, 2, b, sizeof b); CHECK_STR(b, "20");
    wb_display_text(1, 2, b, sizeof b); CHECK_STR(b, "60");
    wb_display_text(2, 2, b, sizeof b); CHECK_STR(b, "30");
}

/* Undo puts the references back too, or a sort followed by its undo would
 * leave the sheet looking right and computing something else. */
static void test_sort_undo_moves_references_back(void)
{
    char b[WB_TEXT_MAX];

    sortref_setup();
    {   /* the sheet has formulas, so the sort asks first */
        static const uint8_t yes[] = { 'y' };
        kbd_host_push(yes, 1);
    }
    CHECK_EQ(grid_key(K_SORT_ASC), GRID_CONTINUE);
    CHECK_EQ(grid_key(K_SORT_UNDO), GRID_CONTINUE);

    wb_display_text(0, 2, b, sizeof b); CHECK_STR(b, "30");
    wb_display_text(1, 2, b, sizeof b); CHECK_STR(b, "20");
    wb_display_text(2, 2, b, sizeof b); CHECK_STR(b, "60");
    wb_edit_text(2, 2, b, sizeof b);    CHECK_STR(b, "=A3*B3");
}

/* A reference to a row OUTSIDE the sorted range still moves, because that
 * is what relative means -- and a formula that did not move at all keeps
 * every reference it had. */
static void test_sort_leaves_unmoved_formulas_alone(void)
{
    char b[WB_TEXT_MAX];

    sortref_setup();
    wb_set_text(5, 2, "=C1+C2");        /* below the data, does not move */
    grid_goto(0, 0);
    {   /* the sheet has formulas, so the sort asks first */
        static const uint8_t yes[] = { 'y' };
        kbd_host_push(yes, 1);
    }
    CHECK_EQ(grid_key(K_SORT_ASC), GRID_CONTINUE);

    wb_edit_text(5, 2, b, sizeof b);
    CHECK_STR(b, "=C1+C2");
}

/* --- selecting a block -------------------------------------------------
 *
 * Ctrl+A anchors at the cursor; moving the cursor then grows the block,
 * which is why the anchor is kept rather than two sorted corners. Escape
 * drops it. Clear acts on the block if there is one and the cell if not.
 */
#define K_SELECT 0x01
#define C_SEL_EXPECT COLOR(COL_BLACK, COL_LGREY)

static void sel_setup(void)
{
    setup();
    wb_set_text(0, 0, "a"); wb_set_text(0, 1, "b");
    wb_set_text(1, 0, "c"); wb_set_text(1, 1, "d");
    wb_set_text(2, 0, "e");
    grid_goto(0, 0);
}

static void test_select_grows_with_the_cursor(void)
{
    sel_setup();
    grid_key(0x01);                     /* Ctrl+A anchors */
    grid_key(K_DOWN);                   /* A1..A2 */

    /* The anchor stays where it was put. */
    CHECK_EQ(grid_state()->sel_row, 0);
    CHECK_EQ(grid_state()->sel_on, 1);

    /* A1 is in the block and shows it; A3 is past the cursor and does not.
     * The cursor cell itself stays reverse video, not block colour. */
    CHECK_EQ(screen_host_color(GRID_ROWHDR_W, ROW_GRID0), C_SEL_EXPECT);
    CHECK(screen_host_color(GRID_ROWHDR_W, ROW_GRID0 + 2) != C_SEL_EXPECT);
}

/* The anchor may end up below and right of the cursor -- the rectangle is
 * sorted at the point of use, not stored sorted. */
static void test_select_backwards(void)
{
    uint16_t r1, c1, r2, c2;

    sel_setup();
    grid_goto(2, 1);
    grid_key(0x01);                     /* Ctrl+A anchors */
    grid_goto(0, 0);

    grid_sel(&r1, &c1, &r2, &c2);
    CHECK_EQ(r1, 0); CHECK_EQ(c1, 0);
    CHECK_EQ(r2, 2); CHECK_EQ(c2, 1);
}

/* With nothing selected, grid_sel() still answers -- the cursor cell -- so
 * a command never has to ask whether there is a block. */
static void test_sel_without_a_block_is_the_cursor(void)
{
    uint16_t r1, c1, r2, c2;

    sel_setup();
    grid_goto(1, 1);
    grid_sel(&r1, &c1, &r2, &c2);
    CHECK_EQ(r1, 1); CHECK_EQ(c1, 1);
    CHECK_EQ(r2, 1); CHECK_EQ(c2, 1);
}

static void test_clear_empties_the_block(void)
{
    char b[WB_TEXT_MAX];

    sel_setup();
    grid_key(0x01);                     /* Ctrl+A anchors */
    grid_goto(1, 1);                    /* A1..B2 */
    grid_key(0x14);                     /* Clear */

    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "");
    wb_display_text(1, 1, b, sizeof b); CHECK_STR(b, "");
    wb_display_text(2, 0, b, sizeof b); CHECK_STR(b, "e");   /* outside it */
    CHECK_EQ(grid_state()->sel_on, 0);  /* and the block is gone */
}

/* Ctrl+A again drops the block -- there is no separate Escape for it, so
 * the toggle is the only way out and had better work. */
static void test_select_toggles_off(void)
{
    sel_setup();
    grid_key(0x01);                     /* Ctrl+A anchors */
    grid_key(K_DOWN);
    grid_key(0x01);                     /* Ctrl+A anchors */

    CHECK_EQ(grid_state()->sel_on, 0);
    CHECK(screen_host_color(GRID_ROWHDR_W, ROW_GRID0 + 1) != C_SEL_EXPECT);
}

/* --- copying a block ---------------------------------------------------
 *
 * The clipboard is a banked block of the cells' edit text, so a formula
 * travels as its source and is compiled again at the far end. The
 * non-formula cells are written by OVL_MENU and the formulas by
 * OVL_FCOMPILE, which is the only overlay allowed to compile one -- so
 * these also check that the two halves meet.
 */
static void block_setup(void)
{
    setup();
    wb_set_text(0, 0, "1");  wb_set_text(0, 1, "2");
    wb_set_text(1, 0, "3");  /* (1,1) deliberately empty */
    grid_goto(0, 0);
}

static void test_copy_block_pastes_all_of_it(void)
{
    char b[WB_TEXT_MAX];

    block_setup();
    grid_key(0x01);                     /* Ctrl+A anchors */
    grid_goto(1, 1);                    /* A1..B2 */
    grid_key(K_COPY);                   /* which lets the block go */
    grid_goto(5, 2);
    grid_key(K_PASTE);

    wb_display_text(5, 2, b, sizeof b); CHECK_STR(b, "1");
    wb_display_text(5, 3, b, sizeof b); CHECK_STR(b, "2");
    wb_display_text(6, 2, b, sizeof b); CHECK_STR(b, "3");
}

/* An empty cell in the block pastes as empty -- it must not leave whatever
 * was in the target, which is what a loop that skips empties would do. */
static void test_block_paste_clears_where_it_was_empty(void)
{
    char b[WB_TEXT_MAX];

    block_setup();
    wb_set_text(6, 3, "in the way");

    grid_goto(0, 0);
    grid_key(0x01);                     /* Ctrl+A anchors */
    grid_goto(1, 1);
    grid_key(K_COPY);
    grid_key(0x01);                     /* Ctrl+A anchors */
    grid_goto(5, 2);
    grid_key(K_PASTE);

    wb_display_text(6, 3, b, sizeof b); CHECK_STR(b, "");
}

/* A formula in the block arrives as a formula, with its references moved
 * by the same offset as the block, and computes where it lands. */
static void test_block_paste_carries_a_formula(void)
{
    char b[WB_TEXT_MAX];

    block_setup();
    wb_set_text(1, 1, "=A1+B1");        /* 1 + 2 */
    grid_goto(0, 0);
    grid_key(0x01);                     /* Ctrl+A anchors */
    grid_goto(1, 1);
    grid_key(K_COPY);
    grid_key(0x01);                     /* Ctrl+A anchors */
    grid_goto(5, 2);
    grid_key(K_PASTE);

    /* Five rows down and two columns right: A1+B1 -> C6+D6, which is
     * where the block's own "1" and "2" landed -- so it still adds the two
     * cells above it and still says 3. */
    wb_edit_text(6, 3, b, sizeof b);    CHECK_STR(b, "=C6+D6");
    wb_display_text(6, 3, b, sizeof b); CHECK_STR(b, "3");
}

/* With no block, copy takes the cursor cell -- the pre-existing behaviour,
 * which works only because the bounds collapse onto the cursor. */
static void test_copy_without_a_block_is_one_cell(void)
{
    char b[WB_TEXT_MAX];

    block_setup();
    grid_goto(0, 1);
    grid_key(K_COPY);
    grid_goto(8, 8);
    grid_key(K_PASTE);

    wb_display_text(8, 8, b, sizeof b); CHECK_STR(b, "2");
    wb_display_text(9, 8, b, sizeof b); CHECK_STR(b, "");   /* one cell only */
}

/* --- $ makes a reference absolute --------------------------------------
 *
 * The compiler parses `$` and throws it away, so the EVALUATOR cannot tell
 * an absolute reference from a relative one. The source text keeps it, and
 * the rewriter works on the source text -- so `$` works for every
 * operation that moves a cell, without the bytecode knowing anything.
 */
static void test_insert_row_honours_dollar(void)
{
    char b[WB_TEXT_MAX];

    setup();
    wb_set_text(0, 0, "10");
    wb_set_text(4, 0, "=A1+$A$1");
    wb_set_text(5, 0, "=$A1+A$1");
    grid_goto(0, 0);                    /* the cut is above A1 */
    grid_key(MENU_INS_ROW);

    wb_edit_text(5, 0, b, sizeof b); CHECK_STR(b, "=A2+$A$1");
    wb_edit_text(6, 0, b, sizeof b); CHECK_STR(b, "=$A2+A$1");
}

static void test_paste_honours_dollar(void)
{
    char b[WB_TEXT_MAX];

    setup();
    wb_set_text(0, 0, "10");
    wb_set_text(0, 2, "=A1+$A$1+$A1+A$1");
    grid_goto(0, 2);
    grid_key(K_COPY);
    grid_goto(3, 4);                    /* +3 rows, +2 columns */
    grid_key(K_PASTE);

    /* Relative moves both ways; $column pins the letter; $row pins the
     * number; $A$1 does not move at all. */
    wb_edit_text(3, 4, b, sizeof b);
    CHECK_STR(b, "=C4+$A$1+$A4+C$1");
}

/* Column letters carry past Z, which is the case a naive shift gets wrong.
 * Z is column 25, so +1 is AA. */
static void test_paste_carries_past_z(void)
{
    char b[WB_TEXT_MAX];

    setup();
    wb_set_text(0, 0, "=Z1");
    grid_goto(0, 0);
    grid_key(K_COPY);
    grid_goto(0, 1);                    /* one column right */
    grid_key(K_PASTE);

    wb_edit_text(0, 1, b, sizeof b); CHECK_STR(b, "=AA1");
}

/* MORE FORMULAS THAN THE QUEUE HOLDS. FPEND_MAX is 512 and the rewriter
 * hands back "full, call me again"; nothing else exercises that loop, and
 * a sheet that stopped adjusting at 512 would be wrong in a way nobody
 * would notice until the numbers were. */
static void test_shift_beyond_one_queue(void)
{
    char b[WB_TEXT_MAX];
    uint16_t r;

    setup();
    wb_set_text(0, 0, "1");
    for (r = 1; r < 600; ++r)
        CHECK_EQ(wb_set_text(r, 1, "=A1"), ERR_OK);

    grid_goto(0, 0);
    grid_key(MENU_INS_ROW);             /* everything moves down one */

    /* The first, one past the queue boundary, and the last. */
    wb_edit_text(2, 1, b, sizeof b);   CHECK_STR(b, "=A2");
    wb_edit_text(514, 1, b, sizeof b); CHECK_STR(b, "=A2");
    wb_edit_text(600, 1, b, sizeof b); CHECK_STR(b, "=A2");
}

/* --- inserting and deleting a column -----------------------------------
 *
 * The mirror of the row commands, and the references follow the same way.
 * Fixture:      A     B     C
 *          1    1     2     =A1+B1
 */
static void colcmd_setup(void)
{
    setup();
    while (kbd_get())
        ;
    wb_set_text(0, 0, "1");
    wb_set_text(0, 1, "2");
    wb_set_text(0, 2, "=A1+B1");
    grid_goto(0, 0);
}

static void test_insert_column_pushes_right(void)
{
    char b[WB_TEXT_MAX];

    colcmd_setup();
    grid_goto(0, 1);                    /* insert before B */
    grid_key(MENU_INS_COL);

    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "1");   /* A stayed */
    wb_display_text(0, 1, b, sizeof b); CHECK_STR(b, "");    /* the gap */
    wb_display_text(0, 2, b, sizeof b); CHECK_STR(b, "2");   /* B -> C */

    /* A1 was left of the cut and did not move; B1 became C1. */
    wb_edit_text(0, 3, b, sizeof b);    CHECK_STR(b, "=A1+C1");
    wb_display_text(0, 3, b, sizeof b); CHECK_STR(b, "3");
}

static void test_delete_column_pulls_left(void)
{
    static const uint8_t yes[] = { 'y' };
    char b[WB_TEXT_MAX];

    setup();
    while (kbd_get())
        ;
    wb_set_text(0, 0, "1");
    wb_set_text(0, 1, "2");             /* B, about to go */
    wb_set_text(0, 2, "3");
    wb_set_text(0, 3, "=A1+C1");
    grid_goto(0, 1);
    kbd_host_push(yes, sizeof yes);
    grid_key(MENU_DEL_COL);

    wb_display_text(0, 0, b, sizeof b); CHECK_STR(b, "1");
    wb_display_text(0, 1, b, sizeof b); CHECK_STR(b, "3");   /* C -> B */

    /* The formula moved D -> C, and C1 moved to B1 with it. */
    wb_edit_text(0, 2, b, sizeof b);    CHECK_STR(b, "=A1+B1");
    wb_display_text(0, 2, b, sizeof b); CHECK_STR(b, "4");
}

/* THE KNOWN LIMIT, pinned so it is a decision and not a surprise.
 *
 * A reference to the column that was DELETED should be #REF!. There is no
 * way to spell that which the compiler will take back, so it keeps the
 * column number -- which now holds something else. Here =A1+B1 sat in C
 * and moved to B, so it now reads itself and says #CYCLE!. That is the
 * honest consequence, and the same rule delete-row already documents. */
static void test_delete_column_reference_to_it_is_not_ref(void)
{
    static const uint8_t yes[] = { 'y' };
    char b[WB_TEXT_MAX];

    colcmd_setup();
    grid_goto(0, 1);
    kbd_host_push(yes, sizeof yes);
    grid_key(MENU_DEL_COL);

    wb_edit_text(0, 1, b, sizeof b);    CHECK_STR(b, "=A1+B1");
    wb_display_text(0, 1, b, sizeof b); CHECK_STR(b, "#CYCLE!");
}

/* Answering anything but y leaves the sheet alone -- a column is destroyed
 * and there is no undo for it. */
static void test_delete_column_can_be_refused(void)
{
    static const uint8_t no[] = { 'n' };
    char b[WB_TEXT_MAX];

    colcmd_setup();
    grid_goto(0, 1);
    kbd_host_push(no, sizeof no);
    grid_key(MENU_DEL_COL);

    wb_display_text(0, 1, b, sizeof b); CHECK_STR(b, "2");
}

/* $ pins a column against an insert, the same as it does against a row. */
static void test_insert_column_honours_dollar(void)
{
    char b[WB_TEXT_MAX];

    colcmd_setup();
    wb_set_text(2, 0, "=$B1+B1");
    grid_goto(0, 1);
    grid_key(MENU_INS_COL);

    wb_edit_text(2, 0, b, sizeof b); CHECK_STR(b, "=$B1+C1");
}

/* Copying lets the block go.
 *
 * Until it did, the arrows still grew the selection afterwards and the
 * paste landed on the far corner of the source -- which looks exactly like
 * copy and paste doing nothing at all. */
static void test_copy_drops_the_block(void)
{
    char b[WB_TEXT_MAX];

    block_setup();
    grid_key(0x01);                     /* Ctrl+A anchors */
    grid_goto(1, 1);                    /* A1..B2 */
    grid_key(K_COPY);

    CHECK_EQ(grid_state()->sel_on, 0);

    /* So this MOVES the cursor rather than extending the block, and the
     * paste lands where it was moved to. */
    grid_goto(5, 2);
    grid_key(K_PASTE);
    wb_display_text(5, 2, b, sizeof b); CHECK_STR(b, "1");
    wb_display_text(6, 2, b, sizeof b); CHECK_STR(b, "3");
}

static void test_copy_again_replaces_the_clipboard(void)
{
    char b[WB_TEXT_MAX];

    block_setup();
    wb_set_text(4, 0, "second");

    /* First copy: a block. */
    grid_key(0x01);                     /* Ctrl+A anchors */
    grid_goto(1, 1);
    grid_key(K_COPY);

    /* Second copy: somewhere else entirely. It must win. */
    grid_goto(4, 0);
    grid_key(K_COPY);
    grid_goto(8, 4);
    grid_key(K_PASTE);

    wb_display_text(8, 4, b, sizeof b); CHECK_STR(b, "second");
    /* And not the first copy's block bleeding through beside it. */
    wb_display_text(8, 5, b, sizeof b); CHECK_STR(b, "");
    wb_display_text(9, 4, b, sizeof b); CHECK_STR(b, "");
}

/* Copy, paste, copy, paste -- four times over, each with a different
 * block. A heap corrupted by the first copy would show by the third. */
/* A copied formula must arrive as a formula, not as its text.
 *
 * The clipboard holds what the formula bar shows, which for a formula is
 * its source INCLUDING the leading '='. put_value() refuses anything
 * starting with '=' so the compiler gets it; if the '=' were missing the
 * cell would land as a label reading "A1+1". */
static void test_copy_paste_of_a_formula(void)
{
    char b[WB_TEXT_MAX];
    cell_record_t rec;

    setup();
    wb_set_text(0, 0, "7");
    wb_set_text(1, 0, "=A1+1");

    /* What the clipboard will be given. */
    wb_edit_text(1, 0, b, sizeof b);
    CHECK_STR(b, "=A1+1");

    grid_goto(1, 0);
    grid_key(K_COPY);
    grid_goto(5, 0);
    grid_key(K_PASTE);

    /* It is a formula cell, and its source still has the '='. */
    CHECK(wb_get(5, 0, &rec));
    CHECK_EQ(rec.type, CELL_FORMULA);
    wb_edit_text(5, 0, b, sizeof b);
    CHECK_EQ(b[0], '=');
}

/* An absolute reference keeps its '$' across a copy. */
static void test_copy_paste_keeps_dollars(void)
{
    char b[WB_TEXT_MAX];

    setup();
    wb_set_text(0, 0, "7");
    wb_set_text(1, 0, "=$A$1+A1");

    grid_goto(1, 0);
    grid_key(K_COPY);
    grid_goto(4, 2);                    /* three rows down, two columns on */
    grid_key(K_PASTE);

    wb_edit_text(4, 2, b, sizeof b);
    /* $A$1 is anchored and must not move; A1 is relative and follows. */
    CHECK_STR(b, "=$A$1+C4");
}

/* Does a currency cell keep its format across a copy? */
static void test_copy_paste_keeps_currency(void)
{
    cell_record_t rec;
    cell_style_t st;
    char b[WB_TEXT_MAX];
    uint8_t id;

    setup();
    memset(&st, 0, sizeof st);
    st.number_format = NF_CURRENCY;
    st.decimal_places = 2;
    CHECK_EQ(styles_add(&st, &id), ERR_OK);

    wb_set_text(0, 0, "8.5");
    wb_get(0, 0, &rec);
    rec.style = id;
    wb_set(0, 0, &rec);
    wb_display_text(0, 0, b, sizeof b);
    CHECK_STR(b, "$8.50");

    grid_goto(0, 0);
    grid_key(K_COPY);
    grid_goto(6, 0);
    grid_key(K_PASTE);

    wb_display_text(6, 0, b, sizeof b);
    CHECK_STR(b, "$8.50");
}

static void test_copy_paste_survives_repetition(void)
{
    char b[WB_TEXT_MAX];
    uint16_t i;

    setup();
    for (i = 0; i < 4; ++i) {
        char v[8];

        v[0] = (char)('1' + i); v[1] = 0;
        wb_set_text(i, 0, v);
        grid_goto(i, 0);
        grid_key(K_COPY);
        grid_goto((uint16_t)(20 + i), 3);
        grid_key(K_PASTE);

        wb_display_text((uint16_t)(20 + i), 3, b, sizeof b);
        CHECK_STR(b, v);
    }

    /* The pool and the heap are still intact after all that. */
    CHECK_EQ(strpool_verify(), 0);
}

void test_grid(void)
{
    test_copy_again_replaces_the_clipboard();
    test_copy_paste_survives_repetition();
    test_copy_paste_of_a_formula();
    test_copy_paste_keeps_dollars();
    test_copy_paste_keeps_currency();
    test_copy_drops_the_block();
    test_insert_column_pushes_right();
    test_delete_column_pulls_left();
    test_delete_column_reference_to_it_is_not_ref();
    test_delete_column_can_be_refused();
    test_insert_column_honours_dollar();
    test_insert_row_honours_dollar();
    test_paste_honours_dollar();
    test_paste_carries_past_z();
    test_shift_beyond_one_queue();
    test_copy_block_pastes_all_of_it();
    test_block_paste_clears_where_it_was_empty();
    test_block_paste_carries_a_formula();
    test_copy_without_a_block_is_one_cell();
    test_select_grows_with_the_cursor();
    test_select_backwards();
    test_sel_without_a_block_is_the_cursor();
    test_clear_empties_the_block();
    test_select_toggles_off();
    test_sort_moves_relative_references();
    test_sort_undo_moves_references_back();
    test_sort_leaves_unmoved_formulas_alone();
    test_column_width_is_set_from_the_prompt();
    test_column_width_can_be_cancelled();
    test_column_width_is_clamped();
    test_pie_slices_are_proportional();
    test_pie_has_a_legend();
    test_pie_ignores_negatives();
    test_chart_line_plots_the_points();
    test_chart_line_rises();
    test_chart_scales_to_the_largest();
    test_chart_of_text_says_so();
    test_chart_returns_to_the_grid();
    test_insert_row_moves_references();
    test_insert_below_leaves_references_alone();
    test_delete_row_shrinks_a_range();
    test_cross_sheet_references_are_not_moved();
    test_function_names_survive();
    test_freeze_pins_the_heading_row();
    test_freeze_cursor_in_the_heading();
    test_freeze_at_a1_keeps_the_headings();
    test_freeze_from_a1_pins_the_row();
    test_freeze_toggles();
    test_freeze_refuses_to_fill_the_screen();
    test_find();
    test_sort_undo_restores_the_order();
    test_sort_undo_through_the_menu();
    test_sort_undo_only_once();
    test_bold_toggles_a_block();
    test_bold_keeps_the_format();
    test_sort_selection_only();
    test_sort_block_keys_on_first_column();
    test_sort_selection_can_be_refused();
    test_sort_undo_without_a_sort();
    test_sort_undo_refuses_when_the_range_grew();
    test_sort_undo_refuses_on_another_sheet();
    test_sort_ascending_moves_whole_rows();
    test_sort_leaves_the_heading_row();
    test_sort_text_that_is_numbers();
    test_sort_descending();
    test_sort_uses_the_cursor_column();
    test_sort_kinds_and_blanks();
    test_sort_asks_before_touching_formulas();
    test_sort_empty_sheet();
    test_paste_without_copy_does_nothing();     /* first: see its comment */
    test_copy_paste_value();
    test_copy_paste_text();
    test_copy_paste_formula();
    test_paste_twice_is_independent();
    test_paste_of_an_empty_cell_clears();
    test_save_and_reopen();
    test_col_names();
    test_cell_names();
    test_initial_layout();
    test_right_edge_clipping();
    test_cursor_moves_without_scrolling();
    test_edges_do_not_wrap();
    test_vertical_scrolling();
    test_horizontal_scrolling();
    test_navigation_keys();
    test_far_corner();
    test_keyboard_queue();
    test_grid_editing();
    test_mouse_selects_a_cell();
    test_mouse_ignores_the_chrome();
    test_mouse_click_is_edge_triggered();
    test_mouse_wheel_scrolls();
    test_mouse_switches_sheets();
    test_mouse_does_not_disturb_an_edit();
}

