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

/* BLACK AND WHITE ARE SWAPPED against the machine's own palette, and the
 * program swaps the two palette entries at start-up to match.
 *
 * The reason is transparency. A text cell shows the layer beneath it only
 * where its background is palette entry 0, and the layer beneath is where
 * cell borders are drawn. The paper is white, so entry 0 has to be white,
 * and the ink moves to entry 1. Every COLOR(fg, bg) in the program then
 * reads and draws exactly as it did.
 *
 * COL_CLEAR is the same index under the name that says what it is for:
 * a background of COL_CLEAR is see-through, and a foreground of it is too.
 *
 * The chart is the exception. It draws into a bitmap on the layer below and
 * wants the machine's own palette, so it puts it back while it is up and
 * uses its own CH_BLACK/CH_WHITE -- see chart.c. */
#define COL_BLACK   1
#define COL_WHITE   0
#define COL_CLEAR   0
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

/* Swap palette entries 0 and 1 for the paper, or put the machine's own back.
 * screen_init() turns it on; the chart turns it off while it is up. */
void    screen_paper(uint8_t on);

/* Turn the border layer off and the palette back. Called from main()'s
 * single exit, beside screen_reset_charset(). */
void    screen_shutdown(void);

/* Draw the next text in bold, or stop. Bold is a second copy of the charset
 * in the upper 128 codes, so this simply sets bit 7 on every character
 * written until it is turned off again. MUST BE TURNED OFF: it is a mode,
 * not an argument, and anything drawn after it is bold too. */
void    screen_bold(uint8_t on);

/* Cell borders.
 *
 * They are drawn on a second layer BEHIND the text, so a border costs the
 * cell no character and no row of its own. A text cell whose background is
 * palette entry 0 is transparent, and entry 0 is the paper -- which is what
 * lets the layer below show through. See screen_init().
 *
 * These bits are STY_BORD_* shifted down by three, and are the index of the
 * tile that draws them. */
#define SCREEN_B_L      0x01
#define SCREEN_B_R      0x02
#define SCREEN_B_T      0x04
#define SCREEN_B_B      0x08

/* Turn the border layer on or off.
 *
 * Off is the normal state and costs nothing: with it off screen_border()
 * returns at once, so a workbook with no borders pays for none of this.
 * Turning it on clears the layer, so call it once per repaint rather than
 * once per cell -- it is cheap to call again when already on. */
void    screen_borders(uint8_t on);

/* Draw one cell's borders across the `len` characters at (x,y). `bits` is
 * SCREEN_B_*: the left edge lands on the first character and the right edge
 * on the last, top and bottom on all of them. Passing 0 clears them, which
 * is how the previous repaint's lines are taken away. */
void    screen_border(uint8_t x, uint8_t y, uint8_t len, uint8_t bits);

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
uint8_t     screen_host_border(uint8_t x, uint8_t y);
#endif

#endif /* X16S_SCREEN_H */
