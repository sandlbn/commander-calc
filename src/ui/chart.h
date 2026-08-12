/* chart.h — a chart of the cursor's column. In OVL_MENU; see chart.c. */
#ifndef X16S_CHART_H
#define X16S_CHART_H

#include <stdint.h>

/* The bitmap a chart is drawn into: 320x240, 4 bits a pixel, at $0:0000.
 * Here rather than in chart.c because the .BMP writer is in an overlay of
 * its own and has to agree about every one of them. See chart.c for why
 * the address is what it is. */
#ifdef __CC65__
#  define PIE_VRAM_HI  0x00
#  define PIE_W        640
#  define PIE_H        240
#  define PIE_STRIDE   320      /* bytes a row at 4bpp */

/* A row offset needs SEVENTEEN bits: 239 * 320 + 319 is 76799. It happens
 * that 65536 / 320 is 204.8, so bit 16 of a row's own offset is simply
 * "y >= 205", and mul() wrapping at sixteen bits leaves exactly the low
 * half. Adding the byte within the row can carry into it as well. */
#  define PIE_HIROW    205
#endif

#define CHART_BAR   0
#define CHART_LINE  1
#define CHART_PIE   2

/* Clears the screen and draws. Leaves it there. */
void chart_draw(uint8_t kind);

/* chart_draw(), then wait for a key, put the sheet back, and say whether
 * the key was S -- which means "write it out". The caller does that: the
 * writer is in OVL_CHARTOUT and an overlay cannot load another.
 *
 * Saving after the screen has been put back is not a mistake. The bitmap
 * stays in VRAM whether or not its layer is being shown, so the pixels are
 * still there to be read. */
uint8_t chart_run(uint8_t kind);

/* Write the bitmap out as CHART.BMP. In OVL_CHARTOUT. */
void chart_bmp_save(void);

#endif /* X16S_CHART_H */
