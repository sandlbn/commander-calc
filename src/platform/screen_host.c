/* screen_host.c — the text screen as a buffer the tests can read.
 *
 * Same semantics as the VERA backend, including that screen_text pads to
 * the full width. Tests assert on whole rendered rows, which is how grid
 * layout gets checked without an emulator.
 */
#include "../ui/screen.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_COLS 128
#define MAX_ROWS 64

static char    chars[MAX_ROWS][MAX_COLS + 1];
static color_t colors[MAX_ROWS][MAX_COLS];
static uint8_t cols = 80, rows = 60;

void screen_host_init(uint8_t c, uint8_t r)
{
    cols = c > MAX_COLS ? MAX_COLS : c;
    rows = r > MAX_ROWS ? MAX_ROWS : r;
    screen_clear(COLOR(COL_WHITE, COL_BLUE));
}

err_t screen_init(void)
{
    screen_host_init(cols, rows);
    return ERR_OK;
}

uint8_t screen_cols(void) { return cols; }
uint8_t screen_rows(void) { return rows; }

static int in_range(uint8_t x, uint8_t y)
{
    return x < cols && y < rows;
}

void screen_put(uint8_t x, uint8_t y, char ch, color_t c)
{
    if (!in_range(x, y))
        return;
    chars[y][x] = ch;
    colors[y][x] = c;
}

void screen_fill(uint8_t x, uint8_t y, uint8_t len, char ch, color_t c)
{
    while (len-- && x < cols)
        screen_put(x++, y, ch, c);
}

void screen_clear(color_t c)
{
    uint8_t y;

    for (y = 0; y < rows; ++y) {
        memset(chars[y], ' ', cols);
        chars[y][cols] = '\0';
        screen_fill(0, y, cols, ' ', c);
    }
}

void screen_text(uint8_t x, uint8_t y, const char *s, uint8_t width, color_t c)
{
    while (width--) {
        screen_put(x++, y, *s ? *s : ' ', c);
        if (*s)
            ++s;
    }
}

void screen_text_right(uint8_t x, uint8_t y, const char *s, uint8_t width,
                       color_t c)
{
    uint8_t len = (uint8_t)strlen(s);
    uint8_t pad;

    if (len >= width) {
        screen_text(x, y, s, width, c);
        return;
    }
    for (pad = width - len; pad; --pad)
        screen_put(x++, y, ' ', c);
    while (*s)
        screen_put(x++, y, *s++, c);
}

void screen_recolor(uint8_t x, uint8_t y, uint8_t len, color_t c)
{
    while (len-- && x < cols) {
        if (in_range(x, y))
            colors[y][x] = c;
        ++x;
    }
}

const char *screen_host_row(uint8_t y)
{
    if (y >= rows)
        return "";
    chars[y][cols] = '\0';
    return chars[y];
}

color_t screen_host_color(uint8_t x, uint8_t y)
{
    return in_range(x, y) ? colors[y][x] : 0;
}

/* Nothing to undo off the machine. */
void screen_reset_charset(void) { }

/* The host screen is a character buffer; weight is not part of it. Kept so
 * the shared UI code needs no target test around the call. */
void screen_bold(uint8_t on) { (void)on; }
