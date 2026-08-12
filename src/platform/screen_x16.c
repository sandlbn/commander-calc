/* screen_x16.c — the text screen as VERA sees it.
 *
 * Layer 1's tile map holds two bytes per cell, character then colour. The
 * base address and map width are read from L1_MAPBASE and L1_CONFIG at
 * startup rather than hardcoded, so this keeps working if the screen mode
 * changes underneath us.
 *
 * All address arithmetic is 16-bit with an explicit carry into bit 16.
 * VRAM is a 17-bit space, and letting cc65 do it in 32-bit longs would cost
 * more than the rest of the routine.
 */
#include "../ui/screen.h"
#include <cbm.h>

#define VERA_ADDR_L     (*(volatile uint8_t *)0x9F20)
#define VERA_ADDR_M     (*(volatile uint8_t *)0x9F21)
#define VERA_ADDR_H     (*(volatile uint8_t *)0x9F22)
#define VERA_DATA0      (*(volatile uint8_t *)0x9F23)
#define VERA_CTRL       (*(volatile uint8_t *)0x9F25)
#define VERA_L1_CONFIG  (*(volatile uint8_t *)0x9F34)
#define VERA_L1_MAPBASE (*(volatile uint8_t *)0x9F35)

/* Auto-increment codes for the top nibble of ADDR_H. */
#define INC1 0x10
#define INC2 0x20

/* screen_x16.s */
extern void     screen_set_iso_charset(void);
extern uint16_t screen_get_size(void);      /* low = columns, high = rows */

/* Golden RAM -- see cfg/x16sheet.cfg. screen_init() sets all of these
 * before anything draws. */
/* In the ZERO PAGE. Every screen_put(), screen_fill() and screen_text() in
 * the program goes through seek(), and seek() reads four of these on every
 * call -- so the byte a zero-page access saves is paid back on every
 * character the program draws. See X16S_ZP_BEGIN in x16sheet.h. */
X16S_ZP_BEGIN
static uint16_t base_lo;      /* map base, low 16 bits */
static uint8_t  base_hi;      /* map base, bit 16 */
static uint16_t row_stride;   /* bytes between rows: map width * 2 */
static uint8_t  cols, rows;
X16S_ZP_END

err_t screen_init(void)
{
    uint8_t map_w_code;

    uint16_t size = screen_get_size();

    cols = (uint8_t)size;
    rows = (uint8_t)(size >> 8);
    if (cols == 0 || rows == 0) {          /* not a text mode */
        cols = 80;
        rows = 60;
    }

    base_lo = (uint16_t)VERA_L1_MAPBASE << 9;
    base_hi = VERA_L1_MAPBASE >> 7;        /* bit 16 of the base address */

    map_w_code = (VERA_L1_CONFIG >> 4) & 3;
    row_stride = (uint16_t)64 << map_w_code;   /* (32 << code) tiles * 2 */

    /* ISO indexes tiles by character code, so ASCII goes straight to VRAM
     * with no screen-code translation anywhere in the program. */
    screen_set_iso_charset();

    return ERR_OK;
}

uint8_t screen_cols(void) { return cols; }
uint8_t screen_rows(void) { return rows; }

/* Point DATA0 at the character byte of (x,y) with the given stride. */
static void seek(uint8_t x, uint8_t y, uint8_t inc)
{
    uint16_t off = (uint16_t)y * row_stride + ((uint16_t)x << 1);
    uint16_t lo  = base_lo + off;
    uint8_t  hi  = base_hi;

    if (lo < base_lo)              /* the add carried into bit 16 */
        ++hi;

    VERA_CTRL   = 0;
    VERA_ADDR_L = (uint8_t)lo;
    VERA_ADDR_M = (uint8_t)(lo >> 8);
    VERA_ADDR_H = (hi & 1) | inc;
}

void screen_put(uint8_t x, uint8_t y, char ch, color_t c)
{
    seek(x, y, INC1);
    VERA_DATA0 = (uint8_t)ch;
    VERA_DATA0 = c;
}

void screen_fill(uint8_t x, uint8_t y, uint8_t len, char ch, color_t c)
{
    seek(x, y, INC1);
    while (len--) {
        VERA_DATA0 = (uint8_t)ch;
        VERA_DATA0 = c;
    }
}

void screen_clear(color_t c)
{
    uint8_t y;

    for (y = 0; y < rows; ++y)
        screen_fill(0, y, cols, ' ', c);
}

void screen_text(uint8_t x, uint8_t y, const char *s, uint8_t width, color_t c)
{
    seek(x, y, INC1);
    while (width--) {
        VERA_DATA0 = *s ? (uint8_t)*s++ : ' ';
        VERA_DATA0 = c;
    }
}

void screen_text_right(uint8_t x, uint8_t y, const char *s, uint8_t width,
                       color_t c)
{
    uint8_t len = 0, pad;

    while (s[len])
        ++len;
    if (len >= width) {
        screen_text(x, y, s, width, c);
        return;
    }
    pad = width - len;
    seek(x, y, INC1);
    while (pad--) {
        VERA_DATA0 = ' ';
        VERA_DATA0 = c;
    }
    while (*s) {
        VERA_DATA0 = (uint8_t)*s++;
        VERA_DATA0 = c;
    }
}

void screen_recolor(uint8_t x, uint8_t y, uint8_t len, color_t c)
{
    /* Start one byte past the character and step by two, so only colour
     * bytes are touched. */
    uint16_t off = (uint16_t)y * row_stride + ((uint16_t)x << 1) + 1;
    uint16_t lo  = base_lo + off;
    uint8_t  hi  = base_hi;

    if (lo < base_lo)
        ++hi;

    VERA_CTRL   = 0;
    VERA_ADDR_L = (uint8_t)lo;
    VERA_ADDR_M = (uint8_t)(lo >> 8);
    VERA_ADDR_H = (hi & 1) | INC2;

    while (len--)
        VERA_DATA0 = c;
}
