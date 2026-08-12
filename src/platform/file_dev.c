/* file_dev.c — which drive we are talking to.
 *
 * Three bytes of resident state, separate from file_io_x16.c because that
 * file lives in an overlay and the directory listing lives in a different
 * one. Both need the device number, and an overlay cannot reach into
 * another overlay.
 */
#include "file_io.h"

static uint8_t device = 8;

#ifndef __CC65__
/* Host only: nothing in the program calls it, only the tests, and on
 * the machine every resident byte is contested. */
void    file_set_device(uint8_t d) { device = d; }
#endif
uint8_t file_device(void) { return device; }
