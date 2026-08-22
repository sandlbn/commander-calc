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
#define VERA_DATA1      (*(volatile uint8_t *)0x9F24)
#define VERA_L1_TILEBASE (*(volatile uint8_t *)0x9F36)
#define VERA_DC_VIDEO    (*(volatile uint8_t *)0x9F29)
#define VERA_L0_CONFIG   (*(volatile uint8_t *)0x9F2D)
#define VERA_L0_MAPBASE  (*(volatile uint8_t *)0x9F2E)
#define VERA_L0_TILEBASE (*(volatile uint8_t *)0x9F2F)

/* The border layer, in VRAM the rest of the machine is not using.
 *
 * $1:7000 is 16K of map, butted up against the text map at $1:B000 and well
 * clear of the mouse pointer's sprite image at $1:3000. The tiles go in the
 * 448 bytes between the ISO charset ($1:F000-$1:F7FF) and the PSG registers
 * at $1:F9C0 -- sixteen tiles is 128 of them, and TILE_BASE has to be 2K
 * aligned, which $1:F800 is. Both survive a chart, which draws its bitmap
 * over $0:0000-$1:2BFF. */
#define BORD_MAP        0x7000          /* bit 16 set, see bord_seek() */
#define BORD_MAPBASE    0xB8            /* $1:7000 >> 9 */
#define BORD_TILEBASE   0xFC            /* $1:F800, 8x8 tiles */
#define BORD_TILES      0xF800
#define BORD_COLOR      COL_MGREY
#define L0_ENABLE       0x10            /* DC_VIDEO */

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
/* 0x80 while bold text is being drawn, 0 otherwise. OR-ed into every
 * character code, which is what selects the emboldened half of the
 * charset -- see make_bold(). Here rather than as an argument because it
 * would otherwise be a fifth parameter on the three hottest calls in the
 * program, and it changes far less often than they are made. */
static uint8_t  bold;
/* Non-zero while the border layer is up. Read by every screen_border(),
 * which is called once per cell drawn, and it is what makes a workbook
 * with no borders in it cost nothing. */
static uint8_t  borders;
X16S_ZP_END

/* Fill the upper 128 character codes with emboldened copies of the lower
 * 128, so that a bold cell is the same text drawn with bit 7 set.
 *
 * VERA gives a text cell one byte of character and one of colour, and
 * nothing else -- there is no attribute bit for weight, and the whole layer
 * shares one charset. Different glyphs are therefore the only way to get
 * bold, and different glyphs mean different character codes.
 *
 * The upper half is where they go. In ISO mode those codes are the accented
 * letters, which this program never draws; giving them up is what buys
 * bold, and it is the one place there was room for a second face.
 *
 * `b | (b >> 1)` smears every pixel one to the right, which is what a bold
 * face is at this size. Bit 7 is the leftmost pixel, so the shift is the
 * right way round.
 *
 * Both VERA data ports at once: ADDR0 walks the plain glyphs and ADDR1 the
 * bold ones, each auto-incrementing, so the loop is a read and a write with
 * no address arithmetic in it at all.
 */
/* Where the character set lives. TILEBASE holds address bits 16:11, so the
 * shift puts them back and bit 16 comes off the top. */
static uint16_t glyph_lo;
static uint8_t  glyph_hi;

static void glyph_base(void)
{
    uint8_t tb = VERA_L1_TILEBASE & 0xFC;

    glyph_lo = (uint16_t)((uint16_t)tb << 9);      /* bits 15..11 */
    glyph_hi = (uint8_t)(tb >> 7);                 /* bit 16      */
}

static void make_bold(void)
{
    uint16_t lo = glyph_lo;
    uint8_t  hi = glyph_hi;
    uint16_t dlo = (uint16_t)(lo + 128 * 8);       /* the upper half */
    uint8_t  dhi = hi;
    uint16_t n;

    if (dlo < lo)                                  /* carried into bit 16 */
        ++dhi;

    VERA_CTRL   = 0;                    /* port 0: read the plain glyphs */
    VERA_ADDR_L = (uint8_t)lo;
    VERA_ADDR_M = (uint8_t)(lo >> 8);
    VERA_ADDR_H = (hi & 1) | INC1;

    VERA_CTRL   = 1;                    /* port 1: write the bold ones */
    VERA_ADDR_L = (uint8_t)dlo;
    VERA_ADDR_M = (uint8_t)(dlo >> 8);
    VERA_ADDR_H = (dhi & 1) | INC1;
    VERA_CTRL   = 0;

    for (n = 0; n < 128 * 8; ++n) {
        uint8_t g = VERA_DATA0;
        VERA_DATA1 = (uint8_t)(g | (g >> 1));
    }
}

/* Generate the sixteen border tiles, one per combination of edges.
 *
 * The tile index IS the set of edges -- SCREEN_B_L is bit 0 and so on --
 * so the renderer can hand a cell's style bits straight over. Drawn rather
 * than tabulated: the loop is smaller than 128 bytes of rodata would be,
 * and rodata here is resident.
 *
 * Bit 7 is the leftmost pixel of a row, so the left edge is 0x80 and the
 * right edge 0x01. The top and bottom edges are whole rows.
 */
static void make_border_tiles(void)
{
    uint8_t n, row;

    VERA_CTRL   = 0;
    VERA_ADDR_L = (uint8_t)BORD_TILES;
    VERA_ADDR_M = (uint8_t)(BORD_TILES >> 8);
    VERA_ADDR_H = 1 | INC1;             /* $1:F800 */

    for (n = 0; n < 16; ++n)
        for (row = 0; row < 8; ++row) {
            uint8_t b = 0;

            if ((n & SCREEN_B_T) && row == 0)
                b = 0xFF;
            if ((n & SCREEN_B_B) && row == 7)
                b = 0xFF;
            if (n & SCREEN_B_L)
                b |= 0x80;
            if (n & SCREEN_B_R)
                b |= 0x01;
            VERA_DATA0 = b;
        }
}

/* Point DATA0 at (x,y) in the border map. Same geometry as the text map --
 * the layer is configured with the text layer's own width -- so the offset
 * arithmetic is the same, on a base that always has bit 16 set. */
static void bord_seek(uint8_t x, uint8_t y)
{
    uint16_t lo = (uint16_t)(BORD_MAP + (uint16_t)y * row_stride
                             + ((uint16_t)x << 1));

    VERA_CTRL   = 0;
    VERA_ADDR_L = (uint8_t)lo;
    VERA_ADDR_M = (uint8_t)(lo >> 8);
    VERA_ADDR_H = 1 | INC1;
}

void screen_borders(uint8_t on)
{
    if (!on) {
        VERA_DC_VIDEO &= (uint8_t)~L0_ENABLE;
        borders = 0;
        return;
    }
    if (borders)
        return;                         /* already up; do not clear it */

    /* Clear before showing: the layer would otherwise flash up whatever the
     * last program left in those 16K. A row at a time, because the map is
     * larger than a 16-bit counter is comfortable with. */
    {
        uint8_t y;
        uint16_t n;

        for (y = 0; y < 64; ++y) {
            bord_seek(0, y);
            for (n = 0; n < 128; ++n) {
                VERA_DATA0 = 0;
                VERA_DATA0 = 0;
            }
        }
    }

    /* The text layer's own map size, so the two grids line up whatever mode
     * the screen is in; 1bpp tiles with T256C, which makes the unset pixels
     * transparent and the set ones the colour in the map. */
    VERA_L0_CONFIG   = (uint8_t)((VERA_L1_CONFIG & 0xF0) | 0x08);
    VERA_L0_MAPBASE  = BORD_MAPBASE;
    VERA_L0_TILEBASE = BORD_TILEBASE;
    VERA_DC_VIDEO   |= L0_ENABLE;
    borders = 1;
}

void screen_border(uint8_t x, uint8_t y, uint8_t len, uint8_t bits)
{
    uint8_t mid, t;

    if (!borders || !len)
        return;

    /* The sides belong to the end characters and the top and bottom to all
     * of them: a ten-character cell gets one left edge, one right edge, and
     * ten each of top and bottom. A one-character cell gets both sides. */
    mid = (uint8_t)(bits & (SCREEN_B_T | SCREEN_B_B));
    t   = (uint8_t)(mid | (bits & SCREEN_B_L));
    bord_seek(x, y);
    while (len--) {
        if (len == 0)
            t |= (uint8_t)(bits & SCREEN_B_R);
        VERA_DATA0 = t;
        VERA_DATA0 = BORD_COLOR;
        t = mid;
    }
}

/* Swap palette entries 0 and 1, or put them back.
 *
 * THIS IS WHAT MAKES BORDERS POSSIBLE. A text cell is transparent where its
 * background is palette entry 0, and transparent is the only way the layer
 * underneath -- the borders -- can be seen. The paper is white, so entry 0
 * has to BE white, and the ink moves to entry 1.
 *
 * Nothing else in the program changes: COL_BLACK and COL_WHITE are defined
 * as the swapped indices, so every COLOR(fg, bg) reads the same and draws
 * the same. The mouse pointer swaps with them and comes out dark on the
 * paper, which is easier to see than the white one was.
 *
 * A palette entry is two bytes: green and blue in the first, red in the
 * second. Entry n is at $1:FA00 + n * 2.
 */
void screen_paper(uint8_t on)
{
    VERA_CTRL   = 0;
    VERA_ADDR_L = 0x00;
    VERA_ADDR_M = 0xFA;
    VERA_ADDR_H = 1 | INC1;             /* $1:FA00, entry 0 */

    if (on) {
        VERA_DATA0 = 0xFF; VERA_DATA0 = 0x0F;   /* 0 := white, the paper */
        VERA_DATA0 = 0x00; VERA_DATA0 = 0x00;   /* 1 := black, the ink   */
    } else {
        VERA_DATA0 = 0x00; VERA_DATA0 = 0x00;   /* 0 := black, as booted */
        VERA_DATA0 = 0xFF; VERA_DATA0 = 0x0F;   /* 1 := white            */
    }
}

void screen_bold(uint8_t on)
{
    bold = on ? 0x80 : 0;
}

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
    glyph_base();
    make_bold();
    make_border_tiles();
    screen_paper(1);
    bold = 0;
    borders = 0;

    return ERR_OK;
}

/* Put the display back the way BASIC expects it: the palette as it was and
 * the border layer off. Called from main()'s single exit, beside
 * screen_reset_charset(). */
void screen_shutdown(void)
{
    screen_borders(0);
    screen_paper(0);
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
    VERA_DATA0 = (uint8_t)ch | bold;
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
        VERA_DATA0 = (*s ? (uint8_t)*s++ : (uint8_t)' ') | bold;
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
        VERA_DATA0 = (uint8_t)' ' | bold;
        VERA_DATA0 = c;
    }
    while (*s) {
        VERA_DATA0 = (uint8_t)*s++ | bold;
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
