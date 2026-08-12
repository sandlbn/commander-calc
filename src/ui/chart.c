/* chart.c — a bar chart of the cursor's column.
 *
 * Lotus 1-2-3's /Graph was the feature that beat VisiCalc, and this is the
 * small honest version of it: the numbers in the cursor's column, from the
 * cursor's row down, drawn as bars, with the column to the left supplying
 * the labels.
 *
 * DRAWN IN COLOUR, NOT IN GLYPHS. A bar is a run of spaces with a coloured
 * background. The screen is in the ISO charset -- the tile index is the
 * character code, which is what lets the rest of the program forget about
 * screen codes -- and ISO-8859-15 has no block-graphic characters to build
 * bars out of. Switching charsets for one screen would mean translating
 * every label; switching VERA to a bitmap mode would mean a framebuffer,
 * a mode restore, and no way to test any of it.
 *
 * Colour costs none of that. It goes through screen_fill() like everything
 * else, so the whole layout -- bar heights, the zero line, which bars fit
 * -- is ordinary arithmetic the host suite can assert on, the same as the
 * grid. That is worth more than sub-character resolution.
 *
 * IN ITS OWN OVERLAY, and that is the pie's doing. Bars and a line fitted
 * beside the menu and Find in OVL_MENU; the pie is another two thousand
 * bytes -- cc65 turns its int16 arithmetic into a great deal of code --
 * and there was nowhere near that much left there.
 *
 * Which means the menu can no longer call this: an overlay cannot load
 * another. The grid dispatches it instead, from the one place that is
 * allowed to swap overlays, and that case is what the resident bytes went
 * on. See grid_key().
 */
#include "chart.h"
#include "screen.h"
#include "grid.h"
#include "menu.h"
#include "../workbook/workbook.h"
#include "../workbook/cells.h"
#include "../formula/formula.h"
#include "../platform/keyboard.h"
#include "../platform/banked_ram.h"
#include "../util/number.h"
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY15")
#  pragma rodata-name (push, "OVL15RO")
#  pragma bss-name (push, "OVL15BSS")
#endif

/* Sixteen. It was twelve when the pie had to be squeezed in beside the
 * other two, and the four values it dropped were worth 44 bytes of an
 * overlay that had none; OVL_CHART has 2 KB now. Seventeen bars fit across
 * eighty columns at four columns each, so sixteen is the last count where
 * every one of them is still labelled. A longer column is charted from the
 * cursor down and stops. */
#define BARS_MAX    16
#define AXIS_W      9           /* room for "-1234.56" and a space */
#define PLOT_TOP    3
#define GAP         1

/* The chart screen is the SAME for all three kinds: the bitmap, at half
 * display scale, so 320x240 pixels with 40x30 characters of text on top.
 * All three draw into the bitmap, which is what makes all three saveable --
 * the .BMP is layer 0 and nothing else.
 *
 * Positions are worked out in CHARACTER cells, as they always were, and
 * scaled to plot units when something is actually drawn. A plot unit is a
 * pixel on the machine and a cell on the host, which is what lets the
 * layout stay testable while the picture gains eight times the resolution
 * in each direction. */
#ifdef __CC65__
#  define CH_SCALE 8
#  define CH_COLS  PIE_COLS
#  define CH_ROWS  PIE_ROWS
#else
#  define CH_SCALE 1
#  define CH_COLS  screen_cols()
#  define CH_ROWS  screen_rows()
#endif
#define CU(n)   ((uint16_t)((n) * CH_SCALE))

#define C_PAPER   COLOR(COL_BLACK, COL_WHITE)
#define C_BAR     COLOR(COL_WHITE, COL_BLUE)
#define C_AXIS    COLOR(COL_BLACK, COL_LGREY)
#define C_TITLE   COLOR(COL_WHITE, COL_MGREY)
/* Like C_BAR, the BACKGROUND nibble is the palette index the pixels take
 * -- bmp_fill() reads it from there. Red so a line is not mistaken for a
 * bar at a glance. */
#define C_LINE    COLOR(COL_WHITE, COL_RED)

/* The pie's screen. A background of colour 0 is TRANSPARENT in VERA's
 * compositor, and the text layer sits above the bitmap -- so this is what
 * lets the pie be seen at all. Any other paper colour would hide it
 * completely. */
#define C_GFX     COLOR(COL_WHITE, COL_BLACK)

/* Ink for text blitted into the bitmap. The LOW nibble is the palette
 * index a glyph pixel takes -- plot1() uses it directly -- where plot()
 * and the swatches take the high one, matching the background convention
 * the slice colours already use. On the host both are just attributes. */
#define C_INK     COLOR(COL_WHITE, COL_BLACK)

static const char S_title[] = "Column ";
static const char S_any[]   = "Any key, S saves";
static const char S_none[]  = "No numbers";
static const char S_pct[]   = "%";

/* Where the pie's legend starts. Clear of the bitmap on the machine --
 * 320x240 pixels is columns 0..39 -- and at the left on the host, where
 * the pie is drawn in cells around the middle of the screen. */
#ifdef __CC65__
#  define LEGEND_X 50           /* clear of a pie 360 pixels wide */
#  define LEGEND_W 12
#else
#  define LEGEND_X 1
#  define LEGEND_W 10
#endif

/* OVL15BSS, not the C stack: this runs under grid_key() and the deepest
 * chain in the program has about seventy bytes to spare. */
static snum_t vals[BARS_MAX];
static uint8_t nvals;

/* The cursor, which every plot needs for its labels. A file static rather
 * than an argument threaded through four functions. */
static const grid_t *gp;

/* The cell's number, whatever kind of cell it is. Text and booleans have
 * none and are skipped, which is what makes a heading row harmless.
 *
 * Reads the record directly rather than parsing wb_display_text(): that
 * text has the cell's format applied, so a currency column comes back as
 * "$1,999.99" and does not parse. */
static uint8_t value_of(uint16_t row, uint16_t col, snum_t *v)
{
    cell_record_t rec;

    if (!cells_get(wb_cells(), row, col, &rec))
        return 0;

    if (rec.type == CELL_NUMBER || rec.type == CELL_DATE) {
        memcpy(v->b, rec.val, 5);
        return 1;
    }
    if (rec.type == CELL_FORMULA) {
        handle_t fr;

        memcpy(&fr, rec.val, sizeof fr);
        if (bank_peek(fr, FR_KIND) != CELL_NUMBER)
            return 0;
        bank_read(fr, FR_VALUE, v->b, 5);
        return 1;
    }
    return 0;
}

/* The scale, as one struct, so row_of() takes a scale rather than four
 * loose numbers that have to be passed in the right order. */
typedef struct {
    snum_t   lo, span, height;
    uint16_t top, h;            /* plot units -- see CU() above */
} scale_t;

static scale_t sc;

/* Where value `v` sits, as a screen row between top and bottom inclusive.
 *
 * The scale always includes zero, so a column of 90..100 draws bars that
 * are nearly full height rather than a row of stubs differing in the last
 * character -- a chart whose baseline is not zero exaggerates, and this one
 * is small enough already without lying as well.
 */
static uint16_t row_of(const snum_t *v)
{
    snum_t t;
    uint16_t n;

    /* Assigned, not initialised: cc65 will not build a struct from a
     * dereferenced pointer in a declaration. */
    t = *v;
    snum_sub(&t, &sc.lo);
    snum_mul(&t, &sc.height);
    if (snum_div(&t, &sc.span) != ERR_OK || snum_to_u16(&t, &n) != ERR_OK)
        n = 0;
    if (n > sc.h)
        n = sc.h;
    return (uint16_t)(sc.top + sc.h - n);
}

/* --- the pie ----------------------------------------------------------
 *
 * Swept, not scanned: for each of 360 rays out from the centre, walk from
 * the middle to the rim filling with the colour of whatever slice that
 * angle falls in. Scanning the bounding box would need an arctangent per
 * cell; sweeping needs only a sine, and a sine is a table.
 *
 * USES ITS OWN MULTIPLY. cc65 satisfies `a * b` from a library module with
 * no overlay of its own, so it would land in the resident image. Shifts and
 * adds here cost nothing resident and are quick enough for a picture drawn
 * once.
 */

/* sin(0..90) x 256, one entry every TWO degrees. The other three quadrants
 * are this one reflected, which is what quarter tables are for.
 *
 * HALF a degree an entry, and angles are counted in half-degrees
 * throughout -- 720 of them to the turn. At a radius of 110 pixels a whole
 * degree leaves the rim samples 1.9 pixels apart, which draws a circle
 * made of dots; a half degree closes it. uint16_t because sin(90) is 256
 * and a byte is not. */
static const uint16_t sintab[181] = {
      0,   2,   4,   7,   9,  11,  13,  16,  18,  20,
     22,  25,  27,  29,  31,  33,  36,  38,  40,  42,
     44,  47,  49,  51,  53,  55,  58,  60,  62,  64,
     66,  68,  71,  73,  75,  77,  79,  81,  83,  85,
     88,  90,  92,  94,  96,  98, 100, 102, 104, 106,
    108, 110, 112, 114, 116, 118, 120, 122, 124, 126,
    128, 130, 132, 134, 136, 138, 139, 141, 143, 145,
    147, 149, 150, 152, 154, 156, 158, 159, 161, 163,
    165, 166, 168, 170, 171, 173, 175, 176, 178, 179,
    181, 183, 184, 186, 187, 189, 190, 192, 193, 195,
    196, 198, 199, 200, 202, 203, 204, 206, 207, 208,
    210, 211, 212, 213, 215, 216, 217, 218, 219, 221,
    222, 223, 224, 225, 226, 227, 228, 229, 230, 231,
    232, 233, 234, 235, 236, 237, 237, 238, 239, 240,
    241, 241, 242, 243, 243, 244, 245, 245, 246, 247,
    247, 248, 248, 249, 249, 250, 250, 251, 251, 252,
    252, 252, 253, 253, 254, 254, 254, 254, 255, 255,
    255, 255, 255, 256, 256, 256, 256, 256, 256, 256,
    256
};

/* --- where a pie pixel goes ------------------------------------------
 *
 * On the machine, a bitmap on layer 0. Layer 1 holds the program's text and
 * composites above it, and a text cell whose background is palette index 0
 * is transparent -- so the chart screen is cleared to a black background,
 * the bitmap shows through, and the title and legend are ordinary text on
 * top. No mode switch and no scale change; getting back is one bit in
 * DC_VIDEO.
 *
 * On the host, plot() writes a character cell instead, which keeps the
 * sweep, the slice arithmetic, the legend and the handling of negatives as
 * one piece of tested code. See docs/design/ui.md.
 */
#ifdef __CC65__

#define VERA_ADDR_L  (*(volatile uint8_t *)0x9F20)
#define VERA_ADDR_M  (*(volatile uint8_t *)0x9F21)
#define VERA_ADDR_H  (*(volatile uint8_t *)0x9F22)
#define VERA_DATA0   (*(volatile uint8_t *)0x9F23)
#define VERA_CTRL    (*(volatile uint8_t *)0x9F25)
#define VERA_DC_VIDEO (*(volatile uint8_t *)0x9F29)
#define VERA_DC_HSCALE (*(volatile uint8_t *)0x9F2A)
#define VERA_DC_VSCALE (*(volatile uint8_t *)0x9F2B)
#define VERA_L0_CONFIG   (*(volatile uint8_t *)0x9F2D)
#define VERA_L0_TILEBASE (*(volatile uint8_t *)0x9F2F)

/* $0:0000, AND THE ADDRESS MATTERS: this is the KERNAL's own bitmap area,
 * unused unless BASIC is drawing graphics. Anywhere in $1:3000-$1:AFFF
 * would run through the sprite image data and destroy the mouse pointer.
 * TILE_BASE holds bits 16:11, so the base must be 2K-aligned; zero is.
 * The full VERA layout is in docs/design/ui.md. */
#define PIE_RAD      180        /* in x; y is half, see draw_pie() */

/* The display is scaled to half for the chart, so 320x240 of bitmap fills
 * the 640x480 screen instead of being drawn twice across it with the
 * second half of every column read from whatever VRAM follows.
 *
 * 640 pixels wide, so HSCALE is left alone and the text keeps all EIGHTY
 * of its columns -- which is what makes the labels readable. Only VSCALE
 * halves, stretching 240 bitmap rows over 480 lines, so the text is 30
 * rows of double-height characters.
 *
 * A bitmap pixel is therefore one output pixel wide and two tall. Anything
 * that has to look round has to allow for it; see draw_pie(). */
#define PIE_SCALE    64
#define PIE_COLS     80
#define PIE_ROWS     30
#define PIE_CX       200
#define PIE_CY       120

/* Written by chart_screen_on() and read by chart_screen_off(), which is only
 * ever called after it. NOT safe to test for "did we turn it on?" --
 * OVL15BSS IS NEVER ZEROED. crt0 clears the BSS segment and nothing else,
 * and an overlay's bss lives past the end of the image the loader reads,
 * so it holds whatever the last overlay in that memory left behind. */
/* Defined below, with the rest of the pixel plumbing. Declared here
 * because clearing the bitmap needs them and that happens first. */
static uint16_t mul(uint8_t a, uint16_t b);
static void vram_seek(uint8_t y, uint16_t xb, uint8_t inc);

static uint8_t saved_vscale;

static void chart_screen_on(void)
{
    uint16_t n;

    VERA_CTRL = 0;
    /* Clear first, then show it: enabling a layer over 38K of whatever was
     * in VRAM would flash the last program's leavings up the screen. */
    /* Row at a time, because 240 * 320 is 76800 and neither the address
     * nor a loop counter fits in the sixteen bits the rest of this uses.
     * vram_seek() knows about bit 16; a flat loop from zero would not. */
    for (n = 0; n < PIE_H; ++n) {
        uint16_t b = PIE_STRIDE;

        vram_seek((uint8_t)n, 0, 0x10);
        while (b--)
            VERA_DATA0 = 0;
    }

    VERA_L0_CONFIG   = 0x06;    /* bitmap mode, 4bpp */
    VERA_L0_TILEBASE = 0x01;    /* base $0:0000, 640 wide (TILEW) */

    /* VSCALE only. The bitmap is a full 640 wide, so the horizontal scale
     * stays as it was and the text keeps its eighty columns. */
    saved_vscale = VERA_DC_VSCALE;
    VERA_DC_VSCALE = PIE_SCALE;
    VERA_DC_VIDEO |= 0x10;      /* layer 0 on */
}

static void chart_screen_off(void)
{
    VERA_DC_VIDEO &= (uint8_t)~0x10;
    VERA_DC_VSCALE = saved_vscale;
}

#else   /* host: no VERA, so a character cell instead of a pixel */

#define PIE_RAD 20
#define PIE_ROWS 59         /* nothing is scaled here; see the note above */
static void chart_screen_on(void)  {}
static void chart_screen_off(void) {}

#endif

/* a*b for small unsigned values, without cc65's helper. */
static uint16_t mul(uint8_t a, uint16_t b)
{
    uint16_t r = 0;

    while (a) {
        if (a & 1)
            r = (uint16_t)(r + b);
        b = (uint16_t)(b << 1);
        a = (uint8_t)(a >> 1);
    }
    return r;
}

/* sin of `d`, x256, signed. `d` is in half-degrees, 0..719. */
static int16_t sin_of(uint16_t d)
{
    if (d < 180) return (int16_t)sintab[d];
    if (d < 360) return (int16_t)sintab[360 - d];
    if (d < 540) return (int16_t)-(int16_t)sintab[d - 360];
    return (int16_t)-(int16_t)sintab[720 - d];
}

/* Not `(d + 180) % 720`: modulo by a non-power-of-two is a call into
 * cc65's division helper, which is a library module with no overlay of its
 * own. One subtraction says the same thing. */
static int16_t cos_of(uint16_t d)
{
    d = (uint16_t)(d + 180);
    return sin_of(d >= 720 ? (uint16_t)(d - 720) : d);
}

/* --- text, drawn INTO the bitmap --------------------------------------
 *
 * ON THE BITMAP, not on the text layer: the .BMP is layer 0 and nothing
 * else, so a legend drawn as text would be missing from every saved file.
 *
 * The glyphs come from VERA's own charset at $1:F000, eight bytes a
 * character, so there is no second font to keep in step. A character cell
 * is 8x8 and a bitmap pixel is two output pixels at the chart's scale, so a
 * glyph blitted here is the size the text layer would have drawn it.
 */
#ifdef __CC65__

/* Point VERA at a VRAM address, auto-incrementing, for reading the
 * charset a byte a row. */
static void vram_at(uint16_t off, uint8_t hi)
{
    VERA_ADDR_L = (uint8_t)off;
    VERA_ADDR_M = (uint8_t)(off >> 8);
    VERA_ADDR_H = (uint8_t)(hi | 0x10);
}

/* Point VERA at byte `xb` of bitmap row `y`. `inc` is 0 to stay put for a
 * read-modify-write, or $10 to walk forward along the row.
 *
 * SEVENTEEN BITS: at 320 bytes a row, the last row starts at 76480, past
 * what the sixteen-bit arithmetic here holds. Two things make that cheap.
 * 65536/320 is 204.8, so bit 16 of a row's offset is exactly `y >= 205`;
 * and mul() wrapping at sixteen bits leaves precisely the low half, so
 * nothing needs masking. Adding the byte within the row can carry into bit
 * 16 too, which is the `nl < lo` test. */
static void vram_seek(uint8_t y, uint16_t xb, uint8_t inc)
{
    uint16_t lo = mul(y, PIE_STRIDE);
    uint16_t nl = (uint16_t)(lo + xb);
    uint8_t  hi = (uint8_t)(y >= PIE_HIROW);

    if (nl < lo)
        ++hi;
    VERA_ADDR_L = (uint8_t)nl;
    VERA_ADDR_M = (uint8_t)(nl >> 8);
    VERA_ADDR_H = (uint8_t)(hi | inc);
}

/* One pixel, read-modify-write. plot() sets a 2x2 block, which is right
 * for a swept pie and wrong for a letter. */
static void plot1(uint16_t x, uint16_t y, uint8_t col)
{
    uint8_t v;

    vram_seek((uint8_t)y, (uint16_t)(x >> 1), 0);
    v = VERA_DATA0;
    if (x & 1)
        v = (uint8_t)((v & 0xF0) | col);
    else
        v = (uint8_t)((v & 0x0F) | (uint8_t)(col << 4));
    VERA_DATA0 = v;
}

static void bmp_text(uint8_t cx, uint8_t cy, const char *s, uint8_t n,
                     color_t col)
{
    uint8_t ink = (uint8_t)(col & 0x0F);
    uint16_t px = (uint16_t)cx << 3;
    uint16_t py = (uint16_t)cy << 3;

    while (n-- && *s) {
        uint16_t src = (uint16_t)(0xF000 + ((uint16_t)(uint8_t)*s << 3));
        uint8_t row;

        for (row = 0; row < 8; ++row) {
            uint8_t bits, i;

            vram_at((uint16_t)(src + row), 0x01);
            bits = VERA_DATA0;
            for (i = 0; i < 8; ++i, bits = (uint8_t)(bits << 1))
                if (bits & 0x80)
                    plot1((uint16_t)(px + i), (uint16_t)(py + row), ink);
        }
        px = (uint16_t)(px + 8);
        ++s;
    }
}

/* A block of colour in the bitmap, for the legend swatches -- screen_fill
 * would put them on the text layer, where the file cannot see them. */
static void bmp_swatch(uint8_t cx, uint8_t cy, uint8_t w, color_t col)
{
    uint16_t x, y;

    for (y = (uint16_t)cy << 3; y < (uint16_t)((cy << 3) + 8); ++y)
        for (x = (uint16_t)cx << 3; x < (uint16_t)((cx + w) << 3); ++x)
            plot1(x, y, (uint8_t)(col >> 4));
}

/* A run of pixels. Byte at a time with auto-increment, not plot1() per
 * pixel: two pixels share a byte at 4bpp, so a filled bar costs half as
 * many writes and none of them a read. `x` and `w` are rounded down to the
 * byte, which is two pixels -- imperceptible on a bar and worth the
 * difference between a chart appearing at once and visibly painting. */
static void bmp_fill(uint16_t x, uint8_t y, uint16_t w, uint8_t h,
                     color_t col)
{
    uint8_t v = (uint8_t)((col & 0xF0) | (col >> 4));

    while (h--) {
        uint16_t n = (uint16_t)((w + 1) >> 1);

        vram_seek(y++, (uint16_t)(x >> 1), 0x10);
        while (n--)
            VERA_DATA0 = v;
    }
}

#else

/* On the host a "pixel" is a character cell, which is what keeps the whole
 * layout -- bar heights, the zero line, which bars fit -- assertable here
 * rather than only visible in a screenshot. */
static void plot1(uint16_t x, uint16_t y, uint8_t col)
{
    if (x < screen_cols() && y < screen_rows())
        screen_put((uint8_t)x, (uint8_t)y, ' ', (color_t)col);
}

static void bmp_fill(uint16_t x, uint8_t y, uint16_t w, uint8_t h,
                     color_t col)
{
    while (h--)
        screen_fill((uint8_t)x, y++, (uint8_t)w, ' ', col);
}

#define bmp_text(cx, cy, s, n, col)  screen_text(cx, cy, s, n, col)
#define bmp_swatch(cx, cy, w, col)   screen_fill(cx, cy, (uint8_t)((w) * 2), \
                                                 ' ', col)
#endif

/* One point of the pie. `col` is a text attribute either way: on the
 * machine its BACKGROUND nibble is the palette index the pixel takes,
 * which keeps one colour table for both.
 *
 * On the machine it marks a 2x2 BLOCK rather than a single pixel, which is
 * what makes the pie solid: rays a half-degree apart are 0.9 pixels apart
 * at the rim before rounding and up to two after it, so single pixels leave
 * the slices speckled with holes.
 *
 * The block also costs less. Two pixels of a 4bpp row share a byte, so
 * writing both nibbles needs no read first, where one pixel means
 * read-modify-write. */
static void plot(int16_t x, int16_t y, color_t col)
{
#ifdef __CC65__
    uint8_t v;

    if (x < 0 || x >= PIE_W - 1 || y < 0 || y >= PIE_H - 1)
        return;

    /* The colour in both nibbles: one byte is the two pixels sharing it,
     * and `>> 1` has already dropped x to that byte's boundary. */
    v = (uint8_t)((col & 0xF0) | (col >> 4));

    /* Two rows, sought separately: a row is 320 bytes and adding that to
     * the offset can carry into bit 16, which vram_seek() works out from
     * the row number rather than from an address that has already lost
     * it. */
    vram_seek((uint8_t)y, (uint16_t)((uint16_t)x >> 1), 0);
    VERA_DATA0 = v;
    vram_seek((uint8_t)(y + 1), (uint16_t)((uint16_t)x >> 1), 0);
    VERA_DATA0 = v;
#else
    if (x >= 0 && x < screen_cols() && y >= 0 && y < screen_rows())
        screen_put((uint8_t)x, (uint8_t)y, ' ', col);
#endif
}

/* r * s / 256, keeping the sign. */
static int16_t scale_by(uint8_t r, int16_t s)
{
    uint16_t m = mul(r, (uint16_t)(s < 0 ? -s : s));

    m >>= 8;
    return s < 0 ? (int16_t)-(int16_t)m : (int16_t)m;
}

static void draw_pie(uint8_t bottom)
{
    /* Slice boundaries in degrees, cumulative. bound[i] is where slice i
     * ends; the last is 360 by construction. */
    static uint16_t bound[BARS_MAX];
    static const color_t slice_col[6] = {
        COLOR(COL_WHITE, COL_BLUE),   COLOR(COL_BLACK, COL_YELLOW),
        COLOR(COL_WHITE, COL_RED),    COLOR(COL_BLACK, COL_GREEN),
        COLOR(COL_BLACK, COL_CYAN),   COLOR(COL_WHITE, COL_PURPLE)
    };

    snum_t total, acc, t, full, hundred;
    uint8_t i, k, r, cx, cy, rad, ci = 0;
    uint16_t d;
    char buf[16];

    /* Negatives have no meaning in a pie: a slice cannot be less than
     * nothing. They are dropped, and their absence is why the percentages
     * are worked out from the total of what is left rather than from the
     * column. */
    snum_from_i16(&full, 720);   /* half-degrees; see sin_of() */
    snum_from_i16(&hundred, 100);

    total = snum_zero;
    for (i = 0; i < nvals; ++i)
        if (snum_sign(&vals[i]) > 0)
            snum_add(&total, &vals[i]);
    if (snum_is_zero(&total)) {
        screen_text(1, PLOT_TOP, S_none, (uint8_t)(sizeof S_none - 1),
                    C_PAPER);
        return;
    }

    acc = snum_zero;
    for (i = 0; i < nvals; ++i) {
        uint16_t deg = 720;

        if (snum_sign(&vals[i]) > 0)
            snum_add(&acc, &vals[i]);
        t = acc;
        snum_mul(&t, &full);
        if (snum_div(&t, &total) == ERR_OK)
            snum_to_u16(&t, &deg);
        bound[i] = deg > 720 ? 720 : deg;
    }
    bound[nvals - 1] = 720;

    rad = PIE_RAD;
#ifdef __CC65__
    cx = PIE_CX;
    cy = PIE_CY;
#else
    cx = (uint8_t)(screen_cols() >> 1);
    cy = (uint8_t)(PLOT_TOP + rad);
#endif
    for (d = 0; d < 720; ++d) {
        int16_t cs = cos_of(d), sn = sin_of(d);
        color_t col;

        for (k = 0; k < nvals; ++k)
            if (d < bound[k])
                break;
        if (k >= nvals)
            k = (uint8_t)(nvals - 1);
        while (k >= 6)                  /* six colours, cycled; see above */
            k = (uint8_t)(k - 6);
        col = slice_col[k];

        /* x gets the whole radius and y gets half of it. A bitmap pixel
         * is one output pixel wide and two tall at this scale, so an
         * ellipse of 2:1 in the bitmap is a circle on the screen. */
        for (r = 1; r <= rad; ++r)
            plot((int16_t)cx + scale_by(r, cs),
                 (int16_t)cy - (scale_by(r, sn) >> 1), col);
    }

    /* A key down the side: the label, its colour, and its share. Without
     * it a pie is six colours and no information. */
    for (i = 0; i < nvals; ++i) {
        uint8_t y = (uint8_t)(PLOT_TOP + i);
        uint16_t pct = 0;

        bmp_swatch(LEGEND_X, y, 1, slice_col[ci]);
        if (++ci == 6)
            ci = 0;

        if (gp->cur_col == 0)
            buf[0] = '\0';
        else
            wb_display_text((uint16_t)(gp->cur_row + i),
                            (uint16_t)(gp->cur_col - 1), buf, sizeof buf);
        bmp_text((uint8_t)(LEGEND_X + 3), y, buf, LEGEND_W, C_INK);

        t = vals[i];
        if (snum_sign(&t) < 0)
            t = snum_zero;
        snum_mul(&t, &hundred);
        if (snum_div(&t, &total) == ERR_OK)
            snum_to_u16(&t, &pct);
        snum_from_i16(&t, (int16_t)pct);
        snum_to_text(&t, buf);
        bmp_text((uint8_t)(LEGEND_X + 4 + LEGEND_W), y, buf, 3, C_INK);
        bmp_text((uint8_t)(LEGEND_X + 7 + LEGEND_W), y, S_pct, 1, C_INK);
    }
}

/* --- the plots --------------------------------------------------------
 *
 * Bars and a line share everything up to here -- the same values, the same
 * zero-based scale, the same axis. They differ only in what they put in
 * the plot area, so that is all that is split out.
 */
static void draw_bars(uint8_t w, uint8_t w1, uint16_t yz)
{
    uint8_t i, cx = AXIS_W;

    for (i = 0; i < nvals; ++i, cx = (uint8_t)(cx + w)) {
        uint16_t y = row_of(&vals[i]), y0, n;

        if ((uint8_t)(cx + w1) > CH_COLS)
            break;

        /* Up from the zero line, or down from it for a negative. */
        if (y <= yz) {
            y0 = y;
            n  = (uint16_t)(yz - y);
        } else {
            y0 = (uint16_t)(yz + 1);
            n  = (uint16_t)(y - yz);
        }
        if (n)
            bmp_fill(CU(cx), (uint8_t)y0, CU(w1), (uint8_t)n, C_BAR);
    }
}

/* A line joining the points, marked at each one.
 *
 * Plain characters here, not colour: a line is one cell wide and a
 * coloured cell with nothing in it reads as a dot of noise, where '*' and
 * '|' read as a chart. The ISO charset has both, which is exactly what it
 * does not have for the solid blocks a bar needs.
 *
 * The joining segment is a vertical run rather than a true diagonal.
 * Bresenham would draw a nicer line and costs a hundred bytes and a
 * multiply-free rewrite of the step; at one cell per column with fourteen
 * points across eighty columns, the run is what a diagonal would round to
 * anyway. */
static void draw_line(uint8_t w, uint8_t w1)
{
    uint8_t i, cx = (uint8_t)(AXIS_W + (w1 >> 1));
    uint16_t prev = 0;

    for (i = 0; i < nvals; ++i, cx = (uint8_t)(cx + w)) {
        uint16_t y = row_of(&vals[i]);

        if (cx >= CH_COLS)
            break;

        /* The joining segment is a vertical run rather than a true
         * diagonal. Bresenham draws a nicer line and costs a hundred bytes
         * and a multiply-free rewrite of the step; at a dozen points
         * across the screen the run is what a diagonal rounds to anyway. */
        if (i) {
            uint16_t a = y < prev ? y : prev;
            uint16_t n = (uint16_t)((y < prev ? prev : y) - a);

            if (n)
                bmp_fill(CU(cx), (uint8_t)a, CH_SCALE, (uint8_t)n, C_LINE);
        }
        /* The point itself, a little thicker so it reads as a marker. */
        bmp_fill((uint16_t)(CU(cx) - (CH_SCALE >> 1)),
                 (uint8_t)(y - (CH_SCALE >> 1)),
                 (uint16_t)(CH_SCALE * 2), (uint8_t)CH_SCALE, C_LINE);
        prev = y;
    }
}

void chart_draw(uint8_t kind)
{
    const cellstore_t *cs = wb_cells();
    snum_t hi;
    uint16_t r;
    uint8_t bottom, h, i, w, x0, w1, room;
    uint16_t yz;
    char buf[16];

    gp = grid_state();
    bottom = (uint8_t)(CH_ROWS - 3);
    h = (uint8_t)(bottom - PLOT_TOP);

    /* Gather first: a chart of nothing should say so without having
     * already cleared the sheet off the screen. */
    /* At most as many as can be LABELLED, not as many as fit.
     *
     * Four columns a bar: three for the name and one for the gap, which is
     * enough for a month or a short code. On the machine's eighty columns
     * that allows seventeen and BARS_MAX is the real limit; it mattered
     * when the chart screen was forty columns wide and twelve bars left
     * each label truncated to a single letter. It is kept because it is
     * the rule that was actually wanted, not the number that came out
     * of it.
     *
     * A pie labels down the side instead and is not constrained by this,
     * but the difference is not worth a second gathering loop. */
    room = (uint8_t)((CH_COLS - AXIS_W) >> 2);
    if (room > BARS_MAX)
        room = BARS_MAX;

    nvals = 0;
    for (r = gp->cur_row; r <= cs->max_row && nvals < room; ++r)
        if (value_of(r, gp->cur_col, &vals[nvals]))
            ++nvals;

    /* Transparent paper for every kind now: the plot is on layer 0 and the
     * text layer above it has to let it through. */
    screen_clear(C_GFX);
    chart_screen_on();

    grid_col_name(gp->cur_col, buf);
    screen_fill(0, 0, CH_COLS, ' ', C_TITLE);
    screen_text(1, 0, S_title, (uint8_t)(sizeof S_title - 1), C_TITLE);
    screen_text((uint8_t)(sizeof S_title), 0, buf, 3, C_TITLE);
    /* The pie halves the display scale, so only the top-left 40x30 of the
     * text map is on screen -- row 58 is not. */
    screen_text(1, (uint8_t)(CH_ROWS - 1), S_any,
                (uint8_t)(sizeof S_any - 1), C_TITLE);

    if (nvals == 0) {
        screen_text(1, PLOT_TOP, S_none, (uint8_t)(sizeof S_none - 1),
                    C_PAPER);
        return;
    }

    /* A pie has no axis and no zero-based scale -- it is shares of a
     * total, not values against a rule -- so it leaves here before any of
     * that is worked out. */
    if (kind == CHART_PIE) {
        draw_pie(bottom);
        return;
    }


    /* The scale, always including zero -- see row_of(). */
    sc.lo = hi = vals[0];
    for (i = 1; i < nvals; ++i) {
        if (snum_cmp(&vals[i], &sc.lo) < 0) sc.lo = vals[i];
        if (snum_cmp(&vals[i], &hi) > 0) hi = vals[i];
    }
    if (snum_sign(&sc.lo) > 0) sc.lo = snum_zero;
    if (snum_sign(&hi) < 0) hi = snum_zero;

    sc.span = hi;
    snum_sub(&sc.span, &sc.lo);
    if (snum_is_zero(&sc.span))
        sc.span = snum_one;         /* a flat column: every bar at the top */

    sc.top = CU(PLOT_TOP);
    sc.h   = CU(h);
    snum_from_i16(&sc.height, (int16_t)sc.h);

    /* Both ends of the scale, up the left edge. */
    snum_to_text(&hi, buf);
    bmp_text(0, PLOT_TOP, buf, AXIS_W - 1, C_INK);
    snum_to_text(&sc.lo, buf);
    bmp_text(0, bottom, buf, AXIS_W - 1, C_INK);

    /* One bar per value, sharing the width that is left.
     *
     * Counted down rather than divided, and the bars are stepped along by
     * adding rather than by multiplying. Not style: cc65's divide and
     * multiply helpers are library modules with no overlay of their own,
     * so one `/` here would have landed ten bytes of runtime in the
     * resident image -- which is all of it that was left. */
    w = 0;
    {
        uint8_t left = (uint8_t)(CH_COLS - AXIS_W);
        while (left >= nvals) {
            left = (uint8_t)(left - nvals);
            ++w;
        }
    }
    if (w < 1 + GAP)
        w = 1 + GAP;
    w1 = (uint8_t)(w - GAP);

    yz = row_of(&snum_zero);

    /* The zero line, drawn before the plot so it sits underneath. */
    bmp_fill(CU(AXIS_W), (uint8_t)yz, CU((uint8_t)(CH_COLS - AXIS_W)),
             CH_SCALE / 4 + 1, C_AXIS);

    if (kind == CHART_LINE)
        draw_line(w, w1);
    else
        draw_bars(w, w1, yz);

    /* The labels from the column to the left, under the plot. */
    if (gp->cur_col) {
        x0 = AXIS_W;
        for (i = 0; i < nvals && x0 + w1 <= CH_COLS;
             ++i, x0 = (uint8_t)(x0 + w)) {
            wb_display_text((uint16_t)(gp->cur_row + i),
                            (uint16_t)(gp->cur_col - 1), buf, sizeof buf);
            bmp_text(x0, (uint8_t)(bottom + 1), buf, w1, C_INK);
        }
    }
}

/* Draw it, hold it, and put the sheet back. Split from chart_draw() so the
 * host suite can look at the chart: everything this adds destroys it. */
uint8_t chart_run(uint8_t kind)
{
    uint8_t k;

    chart_draw(kind);
    k = kbd_wait();

    /* ONLY for the pie, because only the pie calls chart_screen_on().
     * Running it for every chart restores a display scale that was never
     * saved -- a garbage byte out of uninitialised overlay bss, written
     * straight into DC_HSCALE.
     *
     * Before the repaint, not after: the grid is drawn on the text layer
     * with layer 0 composited underneath, so leaving the bitmap on would
     * show the pie through every cell whose background happens to be
     * black. */
    chart_screen_off();
    grid_render_all();

    /* The caller writes the file if this says so: the writer is in
     * OVL_CHARTOUT, and this is an overlay. Any kind, now that all three
     * are drawn into the same bitmap. */
    (void)kind;
    return (uint8_t)(k == 'S' || k == 's');
}

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
