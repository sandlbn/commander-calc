/* chart_bmp.c — write the chart bitmap out as a .BMP.
 *
 * A separate overlay from the chart drawing: writing a file needs its own
 * copy of the file layer, which would not fit beside the renderer. The
 * bitmap lives in VRAM, so it is still there to be read after the chart
 * overlay has been swapped out for this one.
 *
 * 4bpp indexed maps onto VERA with nothing rearranged -- BMP packs the
 * leftmost pixel of a byte in the high nibble, as VERA does -- and 640
 * pixels is 320 bytes, which divides by four, so there is no row padding.
 * BMP rows run bottom-up, so the last row of the bitmap is written first.
 *
 * EVERY ROW IS WRITTEN TWICE. That is not padding, it is the aspect ratio:
 * the chart screen halves DC_VSCALE, so a bitmap pixel is one wide by two
 * tall and the pie is deliberately swept as a 2:1 ellipse to look round on
 * the glass. Viewers assume square pixels, so the doubling bakes the
 * display's scaling into the file. See docs/design/ui.md.
 */
#include "chart.h"
#include "screen.h"
#include "../platform/keyboard.h"
#define FILEIO_OWNER_CHART
#include "../platform/file_io_ovl.h"
#include "../platform/file_io.h"
#include <string.h>

#ifdef __CC65__

#  pragma code-name (push, "OVERLAY16")
#  pragma rodata-name (push, "OVL16RO")
#  pragma bss-name (push, "OVL16BSS")

#define VERA_ADDR_L  (*(volatile uint8_t *)0x9F20)
#define VERA_ADDR_M  (*(volatile uint8_t *)0x9F21)
#define VERA_ADDR_H  (*(volatile uint8_t *)0x9F22)
#define VERA_DATA0   (*(volatile uint8_t *)0x9F23)

#define BMP_HDR   118           /* 14 file + 40 info + 16 palette entries */
#define BMP_H     (PIE_H * 2)   /* every bitmap row written twice; see above */
#define BMP_SIZE  ((uint32_t)BMP_HDR + (uint32_t)PIE_STRIDE * BMP_H)

/* Half a row. A whole one is 160 bytes; two writes a row costs a loop. */
#define BMP_CHUNK (PIE_STRIDE / 2)
static uint8_t bmp_buf[BMP_CHUNK];

static const char S_bmp[]    = "CHART.BMP";
static const char S_saved[]  = "Saved CHART.BMP";
static const char S_failed[] = "Could not write CHART.BMP";

/* Its own, rather than the ones in chart.c: an overlay cannot call
 * another, and these are four lines each. */
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

static void vram_at(uint16_t off, uint8_t hi)
{
    VERA_ADDR_L = (uint8_t)off;
    VERA_ADDR_M = (uint8_t)(off >> 8);
    VERA_ADDR_H = (uint8_t)(hi | 0x10);     /* auto-increment by 1 */
}

/* The start of bitmap row `y`, in seventeen bits. 320 bytes a row puts the
 * last one at 76480, which does not fit in sixteen; 65536/320 is 204.8, so
 * bit 16 is exactly `y >= 205`, and mul() wrapping leaves the low half.
 * The same reasoning as chart.c's vram_seek(), duplicated because an
 * overlay cannot call another. */
static void vram_row(uint8_t y)
{
    uint16_t lo = mul(y, PIE_STRIDE);

    VERA_ADDR_L = (uint8_t)lo;
    VERA_ADDR_M = (uint8_t)(lo >> 8);
    VERA_ADDR_H = (uint8_t)((y >= PIE_HIROW) | 0x10);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint8_t write_bmp(void)
{
    fstream_t f;
    uint16_t y;
    uint8_t i;

    if (file_open_write(&f, S_bmp) != ERR_OK)
        return 0;

    memset(bmp_buf, 0, 54);
    bmp_buf[0] = 'B'; bmp_buf[1] = 'M';
    put32(bmp_buf + 2, BMP_SIZE);
    put32(bmp_buf + 34, (uint32_t)PIE_STRIDE * BMP_H);

    /* The rest is small numbers into a buffer that is already zero, so
     * they go in as the bytes they are: put32() costs ten bytes a call. */
    bmp_buf[10] = BMP_HDR;              /* offset to the pixels */
    bmp_buf[14] = 40;                   /* info header size */
    bmp_buf[18] = (uint8_t)PIE_W;       /* width, 640 */
    bmp_buf[19] = (uint8_t)(PIE_W >> 8);
    bmp_buf[22] = (uint8_t)BMP_H;       /* height, 480 -- two bytes now */
    bmp_buf[23] = (uint8_t)(BMP_H >> 8);
    bmp_buf[26] = 1;                    /* planes */
    bmp_buf[28] = 4;                    /* bits per pixel */
    bmp_buf[46] = 16;                   /* colours used */
    bmp_buf[50] = 16;                   /* all of them significant */
    if (file_write(&f, bmp_buf, 54) != 54)
        goto bad;

    /* The palette as VERA holds it: two bytes an entry, low byte GB and
     * high byte 0R, four bits a channel. BMP wants eight bits a channel in
     * B,G,R,0 order, so each nibble is doubled -- $F becomes $FF, not $F0,
     * or white would come out three-quarters grey. */
    vram_at(0xFA00, 0x01);
    for (i = 0; i < 16; ++i) {
        uint8_t gb = VERA_DATA0;
        uint8_t r  = (uint8_t)(VERA_DATA0 & 0x0F);
        uint8_t g  = (uint8_t)(gb >> 4);
        uint8_t b  = (uint8_t)(gb & 0x0F);

        bmp_buf[0] = (uint8_t)(b | (b << 4));
        bmp_buf[1] = (uint8_t)(g | (g << 4));
        bmp_buf[2] = (uint8_t)(r | (r << 4));
        bmp_buf[3] = 0;
        if (file_write(&f, bmp_buf, 4) != 4)
            goto bad;
    }

    y = PIE_H;
    while (y--) {                       /* bottom-up */
        /* Twice, and re-seeking each time: reading VERA advances the
         * address, so the second pass cannot just write the buffer again
         * without re-reading the row. See the note at the top. */
        uint8_t rep = 2;

        while (rep--) {
            uint8_t half = 2;

            vram_row((uint8_t)y);
            while (half--) {
                for (i = 0; i < BMP_CHUNK; ++i)
                    bmp_buf[i] = VERA_DATA0;
                if (file_write(&f, bmp_buf, BMP_CHUNK) != BMP_CHUNK)
                    goto bad;
            }
        }
    }

    return file_close(&f) == ERR_OK;

bad:
    file_close(&f);
    return 0;
}

void chart_bmp_save(void)
{
    uint8_t y = (uint8_t)(screen_rows() - 1);
    uint8_t ok = write_bmp();

    /* On the message line, over the sheet that is back by now. The next
     * repaint clears it, which is the right lifetime for this. */
    screen_text(1, y, ok ? S_saved : S_failed,
                (uint8_t)(ok ? sizeof S_saved - 1 : sizeof S_failed - 1),
                COLOR(COL_BLACK, COL_YELLOW));
}

#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)

#else

void chart_bmp_save(void) { }

#endif
