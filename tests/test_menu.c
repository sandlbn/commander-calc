/* test_menu.c — the menu bar.
 *
 * It runs no commands: menu_run() returns the key code of whatever was
 * chosen and the caller dispatches it as if that key had been pressed. So
 * what there is to test is that the right code comes back, and that moving
 * between menus leaves the screen showing one dropdown rather than two.
 *
 * That second one is not fussiness. The first version drew each dropdown
 * without erasing the last, so walking from File to Edit put both on screen
 * at once, overlapping.
 */
#include "test.h"
#include "../src/ui/screen.h"
#include "../src/ui/menu.h"
#include "../src/platform/keyboard.h"
#include "../src/ui/grid.h"
#include "../src/workbook/workbook.h"

#define K_RIGHT 0x1D
#define K_LEFT  0x9D
#define K_DOWN  0x11
#define K_UP    0x91
#define K_ENTER 0x0D
#define K_ESC   0x1B

static void setup(void)
{
    kbd_host_reset();
    screen_host_init(80, 60);
    screen_clear(0);
}

static uint8_t run(const uint8_t *keys, uint8_t n)
{
    setup();
    kbd_host_push(keys, n);
    return menu_run();
}

/* The first item of the first menu is New, which is F1. */
static void test_first_item(void)
{
    static const uint8_t k[] = { K_ENTER };
    CHECK_EQ(run(k, sizeof k), 0x85);
}

/* Down three times from New reaches Save as, which is F6. */
static void test_walk_down(void)
{
    static const uint8_t k[] = { K_DOWN, K_DOWN, K_DOWN, K_ENTER };
    CHECK_EQ(run(k, sizeof k), 0x8B);
}

/* Up from the first item wraps to the last: Quit, F9. */
static void test_wrap_up(void)
{
    static const uint8_t k[] = { K_UP, K_ENTER };
    CHECK_EQ(run(k, sizeof k), 0x10);
}

/* Right moves to Edit, whose first item is Edit cell, F2. */
static void test_second_menu(void)
{
    static const uint8_t k[] = { K_RIGHT, K_ENTER };
    CHECK_EQ(run(k, sizeof k), 0x89);
}

/* Left from the first menu wraps to the last, which is Sheet: its first
 * item is Next, and the sheet commands are dispatched by grid_key() the
 * same way a key is. */
static void test_wrap_left(void)
{
    static const uint8_t k[] = { K_LEFT, K_ENTER };
    CHECK_EQ(run(k, sizeof k), MENU_SHEET_NEXT);
}

/* Escape chooses nothing, and the caller must not dispatch a key. */
static void test_cancel(void)
{
    static const uint8_t k[] = { K_ESC };
    CHECK_EQ(run(k, sizeof k), 0);
}

/* A command key pressed with the menu open still works. */
static void test_key_passes_through(void)
{
    static const uint8_t k[] = { 0x88 };        /* F7, import */
    CHECK_EQ(run(k, sizeof k), 0x88);
}

/* One dropdown on screen, not two. Walking to Edit and back must leave no
 * trace of the menu that is no longer open. */
static void test_one_dropdown(void)
{
    static const uint8_t k[] = { K_RIGHT, K_ESC };
    const char *row;

    setup();
    kbd_host_push(k, sizeof k);
    menu_run();

    /* Edit's first item is on row 1. File's second item, "Open", would be
     * on row 2 if its dropdown had survived. */
    row = screen_host_row(2);
    CHECK(row[2] != 'O');       /* not the 'O' of "Open" */
    CHECK(row[2] != 'S');       /* nor "Save", one row further on */
}

/* The dropdown is as wide as its text needs and no wider.
 *
 * It used to clear a fixed 27x7 rectangle in the menu's own colour before
 * drawing, so every menu painted a grey slab the size of the largest one:
 * File's seven thirteen-character items sat in a panel twice their width,
 * and Sheet's five short ones looked worse still.
 *
 * Colour is the only way to see this -- the panel is spaces either way --
 * which is what screen_host_color is for. */
#define C_ITEM_EXPECT COLOR(COL_WHITE, COL_MGREY)

static void test_panel_fits_its_text(void)
{
    static const uint8_t k[] = { K_ESC };

    setup();
    kbd_host_push(k, sizeof k);
    menu_run();

    /* File opens at column 1. Its longest label is "New        F1", which
     * is 13, so the panel is 15 wide: columns 1..15.
     *
     * Row 2, not row 1: the first item is the selected one and is drawn in
     * the highlight colour. */
    CHECK_EQ(screen_host_color(1, 2), C_ITEM_EXPECT);
    CHECK_EQ(screen_host_color(15, 2), C_ITEM_EXPECT);

    /* And stops there. Column 16 belongs to the sheet again. */
    CHECK(screen_host_color(16, 2) != C_ITEM_EXPECT);

    /* Seven items, so seven rows and no eighth. */
    CHECK_EQ(screen_host_color(1, 7), C_ITEM_EXPECT);
    CHECK(screen_host_color(1, 8) != C_ITEM_EXPECT);
}

/* The panel is sized to its text, which is the whole point of measuring
 * rather than reserving -- so these numbers follow the Sheet menu's table
 * in menu.c and have to be updated with it:
 *
 *   width  = longest label + 2   ("Rename..." is 9, so 11)
 *   items  = 5
 *
 * It starts at x=34 (the bar reads " File  Edit  Layout  Data  Chart
 * Sheet") and its first item is on row 1, under the bar. Freeze and Column
 * width moved to Layout, which is what took this from 7 items to 5 and
 * from 14 wide to 11 -- the panel measures its text, so both had to move
 * together. */
#define SHEET_X     34
#define SHEET_W     11
#define SHEET_ITEMS 5
/* Every menu's dropdown opens under its own title.
 *
 * menu.c carries the comment "the x of each title has to match S_bar below,
 * character for character. Nothing checks that for you." Now something
 * does. There are two copies of the bar -- menu.c's and grid.c's, because
 * an overlay load per screen redraw would be absurd -- and a table of x
 * positions that has to agree with both. Adding the Layout menu renumbered
 * four of the six, which is exactly the edit that gets this wrong.
 *
 * Walking right from File visits them in table order, so the nth menu is n
 * presses away. The panel's left edge is where its title starts, and the
 * bar's own text says where that is. */
static const uint8_t menu_x[] = { 1, 7, 13, 21, 27, 34 };
static const char menu_initial[] = "FELDCS";

static void test_each_menu_opens_under_its_title(void)
{
    uint8_t i;

    for (i = 0; i < sizeof menu_x; ++i) {
        uint8_t k[8];
        uint8_t n = 0;
        const char *bar;
        uint8_t x = menu_x[i];

        while (n < i)
            k[n++] = K_RIGHT;
        k[n++] = K_ESC;

        setup();
        kbd_host_push(k, n);
        menu_run();

        /* The title is drawn where the table says it is... */
        bar = screen_host_row(0);
        CHECK_EQ(bar[x], menu_initial[i]);
        CHECK_EQ(bar[x - 1], ' ');

        /* ...and the panel hangs from that same column, not one over. */
        CHECK_EQ(screen_host_color(x, 2), C_ITEM_EXPECT);
        CHECK(x == 0 || screen_host_color(x - 1, 2) != C_ITEM_EXPECT);
    }
}

static void test_short_menu_gets_a_short_panel(void)
{
    static const uint8_t k[] = { K_LEFT, K_ESC };

    setup();
    kbd_host_push(k, sizeof k);
    menu_run();

    CHECK_EQ(screen_host_color(SHEET_X, 2), C_ITEM_EXPECT);
    CHECK_EQ(screen_host_color(SHEET_X + SHEET_W - 1, 2), C_ITEM_EXPECT);
    CHECK(screen_host_color(SHEET_X + SHEET_W, 2) != C_ITEM_EXPECT);

    CHECK_EQ(screen_host_color(SHEET_X, SHEET_ITEMS), C_ITEM_EXPECT);
    CHECK(screen_host_color(SHEET_X, SHEET_ITEMS + 1) != C_ITEM_EXPECT);
}

/* --- inserting and deleting rows --------------------------------------- *
 *
 * Whole rows move, from the cursor's row down, and the columns the moving
 * row does not use have to be cleared as it passes -- otherwise the row it
 * lands on shows through in those columns. That is the bug this checks
 * for, and it is invisible on a sheet where every row is full. So the
 * fixture is deliberately ragged: row 2 has a B and the rest do not.
 *
 *      A       B
 *   1  one
 *   2  two     II
 *   3  three
 */
static void rows_setup(void)
{
    setup();
    CHECK_EQ(wb_init(), ERR_OK);
    CHECK_EQ(wb_set_text(0, 0, "one"), ERR_OK);
    CHECK_EQ(wb_set_text(1, 0, "two"), ERR_OK);
    CHECK_EQ(wb_set_text(1, 1, "II"), ERR_OK);
    CHECK_EQ(wb_set_text(2, 0, "three"), ERR_OK);
    grid.cur_row = grid.cur_col = 0;
    grid.top_row = grid.left_col = 0;

}

static void cell_is(uint16_t r, uint16_t c, const char *want)
{
    char buf[WB_TEXT_MAX];

    wb_display_text(r, c, buf, sizeof buf);
    CHECK_STR(buf, want);
}

static void test_insert_row_pushes_the_rest_down(void)
{
    rows_setup();
    grid.cur_row = 1;                   /* on "two" */

    CHECK_EQ(menu_cmd(MENU_INS_ROW), 1);

    cell_is(0, 0, "one");
    cell_is(1, 0, "");                  /* the new, empty row */
    cell_is(1, 1, "");
    cell_is(2, 0, "two");
    cell_is(2, 1, "II");
    cell_is(3, 0, "three");
}

/* Insert on the top row: nothing above it, everything below it moves. */
static void test_insert_at_the_top(void)
{
    rows_setup();

    CHECK_EQ(menu_cmd(MENU_INS_ROW), 1);

    cell_is(0, 0, "");
    cell_is(1, 0, "one");
    cell_is(3, 0, "three");
}

/* Below the last used row there is nothing to push and nothing to take
 * away, so both commands do nothing and say so. */
static void test_row_commands_below_the_data_do_nothing(void)
{
    static const uint8_t yes[] = { 'y' };

    rows_setup();
    grid.cur_row = 50;

    CHECK_EQ(menu_cmd(MENU_INS_ROW), 0);
    kbd_host_push(yes, sizeof yes);
    CHECK_EQ(menu_cmd(MENU_DEL_ROW), 0);
    cell_is(2, 0, "three");
}

static void test_delete_row_pulls_the_rest_up(void)
{
    static const uint8_t yes[] = { 'y' };

    rows_setup();
    grid.cur_row = 1;                   /* on "two" */
    kbd_host_push(yes, sizeof yes);

    CHECK_EQ(menu_cmd(MENU_DEL_ROW), 1);

    cell_is(0, 0, "one");
    cell_is(1, 0, "three");
    /* "II" was in the deleted row and must not have been left behind in
     * column B when "three" moved up over it. */
    cell_is(1, 1, "");
    cell_is(2, 0, "");
}

/* Anything but Y leaves the sheet alone -- there is no undo for a deleted
 * row, so the question has to be a real one. */
static void test_delete_row_can_be_refused(void)
{
    static const uint8_t no[] = { K_ESC };

    rows_setup();
    grid.cur_row = 1;
    kbd_host_push(no, sizeof no);

    CHECK_EQ(menu_cmd(MENU_DEL_ROW), 0);
    cell_is(1, 0, "two");
    cell_is(2, 0, "three");
}

/* --- replace -----------------------------------------------------------
 *
 * Asks per cell because there is no undo for it. Y changes one, N leaves
 * it, A does the rest without asking, Escape stops. Text cells only.
 */
static void rep_setup(void)
{
    setup();
    CHECK_EQ(wb_init(), ERR_OK);
    wb_set_text(0, 0, "red car");
    wb_set_text(1, 0, "red bus");
    wb_set_text(2, 0, "42");            /* a number: never touched */
    wb_set_text(3, 0, "=A3+1");         /* a formula: never touched */
    grid_goto(0, 0);
    find_text[0] = 0;
    rep_text[0] = 0;    /* both prompts start from what is there */
}

static void cell_is(uint16_t r, uint16_t c, const char *want);

static void test_replace_asks_for_each(void)
{
    /* "red" -> "blue", then Y on the first and N on the second. */
    static const uint8_t k[] = { 'r','e','d',13, 'b','l','u','e',13, 'y', 'n' };

    rep_setup();
    kbd_host_push(k, sizeof k);
    /* 1 = the sheet changed, which is what makes the grid recalculate. */
    CHECK_EQ(menu_cmd(MENU_REPLACE), 1);

    cell_is(0, 0, "blue car");
    cell_is(1, 0, "red bus");           /* N left it alone */
}

static void test_replace_all_does_the_rest(void)
{
    static const uint8_t k[] = { 'r','e','d',13, 'b','l','u','e',13, 'a' };

    rep_setup();
    kbd_host_push(k, sizeof k);
    CHECK_EQ(menu_cmd(MENU_REPLACE), 1);

    cell_is(0, 0, "blue car");
    cell_is(1, 0, "blue bus");
}

/* A NUMBER IS REPLACED and stays a number -- text-only was the first cut
 * and it meant searching a sheet of numbers for "2" found nothing at all,
 * which is exactly what people replace. The result goes back through the
 * same reading a typed value gets, so 42 -> 99 is still a number.
 *
 * A FORMULA IS NOT. Rewriting its source needs the compiler, which is
 * another overlay, and a careless search through one destroys it. */
static void test_replace_changes_numbers_but_not_formulas(void)
{
    static const uint8_t k[] = { '4','2',13, '9','9',13, 'a' };
    char b[WB_TEXT_MAX];

    rep_setup();
    kbd_host_push(k, sizeof k);
    CHECK_EQ(menu_cmd(MENU_REPLACE), 1);

    wb_display_text(2, 0, b, sizeof b); CHECK_STR(b, "99");
    wb_edit_text(3, 0, b, sizeof b);    CHECK_STR(b, "=A3+1");
}

/* Replacing part of a number with something that is not one makes it a
 * label -- the same as typing it would. */
static void test_replace_can_turn_a_number_into_text(void)
{
    static const uint8_t k[] = { '4',13, 'x',13, 'a' };
    char b[WB_TEXT_MAX];

    rep_setup();
    kbd_host_push(k, sizeof k);
    CHECK_EQ(menu_cmd(MENU_REPLACE), 1);

    wb_display_text(2, 0, b, sizeof b); CHECK_STR(b, "x2");
}

/* Escape at the prompt changes nothing at all. */
static void test_replace_cancelled(void)
{
    static const uint8_t k[] = { 'r','e','d',13, 27 };

    rep_setup();
    kbd_host_push(k, sizeof k);
    CHECK_EQ(menu_cmd(MENU_REPLACE), 0);

    cell_is(0, 0, "red car");
    cell_is(1, 0, "red bus");
}

/* Is `needle` anywhere on row `y`? The About box is centred, so pinning it
 * to a column would only be re-deriving the arithmetic under test. */
static int row_has(uint8_t y, const char *needle)
{
    return strstr(screen_host_row(y), needle) != NULL;
}

/* Clicking the product name opens the About box rather than a menu.
 *
 * It travels as a column number through menu_open_x, so the thing worth
 * pinning is that the sentinel does not land on a real menu: MENU_ABOUT is
 * 0xFE, and menu_at() would otherwise answer with the last title whose
 * column is below it. */
static void test_about_box(void)
{
    uint8_t k[2];

    setup();

    /* No keys queued: about_run() waits for one, and off the machine an
     * empty queue answers immediately, so this returns without blocking. */
    menu_open_x = MENU_ABOUT;
    CHECK_EQ(menu_run(), 0);            /* no command came back */

    {   /* centred: 40 wide and 9 tall on an 80x60 screen */
        uint8_t y = (uint8_t)((60 - 9) >> 1);
        CHECK(row_has(y, "Commander Calc"));
        CHECK(row_has((uint8_t)(y + 2), "spreadsheet"));
        CHECK(row_has((uint8_t)(y + 6), "any key"));
    }

    /* And it is consumed: the next open is a menu again, not the box. */
    CHECK_EQ(menu_open_x, MENU_BY_KEY);

    k[0] = 0x1B;                        /* ESC closes the bar */
    kbd_host_push(k, 1);
    menu_open_x = 1;                    /* the File title */
    CHECK_EQ(menu_run(), 0);
    CHECK(row_has(0, "File"));
}

void test_menu(void)
{
    test_about_box();
    test_replace_asks_for_each();
    test_replace_all_does_the_rest();
    test_replace_changes_numbers_but_not_formulas();
    test_replace_can_turn_a_number_into_text();
    test_replace_cancelled();
    test_panel_fits_its_text();
    test_short_menu_gets_a_short_panel();
    test_each_menu_opens_under_its_title();
    test_first_item();
    test_walk_down();
    test_wrap_up();
    test_second_menu();
    test_wrap_left();
    test_cancel();
    test_key_passes_through();
    test_one_dropdown();

    test_insert_row_pushes_the_rest_down();
    test_insert_at_the_top();
    test_row_commands_below_the_data_do_nothing();
    test_delete_row_pulls_the_rest_up();
    test_delete_row_can_be_refused();
}
