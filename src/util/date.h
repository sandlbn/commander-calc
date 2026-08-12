/* date.h — Excel's serial day numbers.
 *
 * A date in a spreadsheet is a number -- 1 is 1 January 1900 -- and the
 * display format is what makes it look like a date. Resident, because the
 * grid needs it on every repaint of a date cell.
 *
 * Excel treats 1900 as a leap year: serial 60 is a 29 February that never
 * happened, and every later date is one greater than the true day count.
 * Every file carries the error, so reproducing it is the only way to agree
 * with whatever wrote the file. Corrected for exactly once, in
 * serial_to_date; no other code needs to know.
 */
#ifndef X16S_DATE_H
#define X16S_DATE_H

#include "../x16sheet.h"

void date_from_serial(uint32_t serial, uint16_t *y, uint8_t *m, uint8_t *d);

/* "2026-07-21" into `out`, which needs 11 bytes. Returns the length. */
uint8_t date_format(uint32_t serial, char *out);

#ifdef X16S_HOST
/* Written and tested, not yet resident.
 *
 * date_to_serial has no caller until a cell can be typed as a date, and the
 * time pair is not wired into the renderer because the bytes are not there.
 * Keeping them building and tested on the host is what makes bringing them
 * back a wiring job rather than a rewrite. */
#ifndef __CC65__       /* host only -- see the definition */
uint32_t date_to_serial(uint16_t y, uint8_t m, uint8_t d);
#endif
void     date_time_from_fraction(uint16_t thousandths_of_day, uint8_t *hh,
                                 uint8_t *mm, uint8_t *ss);
#ifndef __CC65__       /* host only -- see the definition */
uint8_t  date_format_time(uint16_t thousandths_of_day, char *out);
#endif
#endif

#endif /* X16S_DATE_H */
