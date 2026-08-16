/* screen.h — the text screen, as a grid of cells with colour attributes.
 *
 * X16  (screen_x16.c)   writes straight into VERA's layer-1 tile map
 * host (screen_host.c)  writes into a buffer the tests can read back
 *
 * That second backend is the point: every pixel of layout arithmetic —
 * column positions, clipping at the right edge, scroll offsets, header
 * labels — is ordinary portable code that can be asserted on natively,
 * instead of eyeballed in an emulator screenshot.
 *
 * Characters are ASCII. The X16 backend switches to the ISO charset at
 * startup, where the VERA tile index is the character code itself, so no
 * screen-code translation happens anywhere in the program.
 */
#ifndef X16S_SCREEN_H
#define X16S_SCREEN_H

#include "../util/errors.h"

/* Attribute byte: high nibble background, low nibble foreground. */
typedef uint8_t color_t;

#define COL_BLACK   0
#define COL_WHITE   1
#define COL_RED     2
#define COL_CYAN    3
#define COL_PURPLE  4
#define COL_GREEN   5
#define COL_BLUE    6
#define COL_YELLOW  7
#define COL_ORANGE  8
#define COL_BROWN   9
#define COL_LRED    10
#define COL_DGREY   11
#define COL_MGREY   12
#define COL_LGREEN  13
#define COL_LBLUE   14
#define COL_LGREY   15

#define COLOR(fg, bg) ((color_t)(((bg) << 4) | (fg)))

err_t   screen_init(void);

/* Undo screen_init()'s charset and ISO-mode switch, so that whatever runs
 * after this program gets the machine it expects. See main(). */
void    screen_reset_charset(void);
uint8_t screen_cols(void);
uint8_t screen_rows(void);

void screen_clear(color_t c);
void screen_put(uint8_t x, uint8_t y, char ch, color_t c);
void screen_fill(uint8_t x, uint8_t y, uint8_t len, char ch, color_t c);

/* Write `s` at (x,y), clipped and then space-padded to exactly `width`
 * columns. Padding rather than stopping is what lets a cell repaint erase
 * the value that used to be there without a separate clear. */
void screen_text(uint8_t x, uint8_t y, const char *s, uint8_t width, color_t c);

/* Same, but right-aligned within `width` — numbers, row headers. */
void screen_text_right(uint8_t x, uint8_t y, const char *s, uint8_t width,
                       color_t c);

/* Change the colour of a run without touching its characters. This is how
 * the cell cursor moves: two recolour calls instead of a repaint. */
void screen_recolor(uint8_t x, uint8_t y, uint8_t len, color_t c);

#ifdef X16S_HOST
/* Test hooks: the rendered contents, as text. */
void        screen_host_init(uint8_t cols, uint8_t rows);
const char *screen_host_row(uint8_t y);
color_t     screen_host_color(uint8_t x, uint8_t y);
#endif

#endif /* X16S_SCREEN_H */
