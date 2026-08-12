#include "date.h"

static const uint8_t month_days[12] = {
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

/* The real rule, which is what makes 1900 not a leap year — and therefore
 * what makes Excel's serial 60 wrong. */
static uint8_t is_leap(uint16_t y)
{
    if (y % 4)
        return 0;
    if (y % 100)
        return 1;
    return (uint8_t)(y % 400 == 0);
}

static uint8_t days_in(uint16_t y, uint8_t m)
{
    if (m == 2 && is_leap(y))
        return 29;
    return month_days[m - 1];
}

void date_from_serial(uint32_t serial, uint16_t *y, uint8_t *m, uint8_t *d)
{
    uint16_t year = 1900;
    uint8_t  mon;
    uint32_t days;

    if (serial == 0) {          /* Excel shows this as 1900-01-00 */
        *y = 1900; *m = 1; *d = 0;
        return;
    }
    if (serial == 60) {         /* the day that never happened */
        *y = 1900; *m = 2; *d = 29;
        return;
    }

    /* Past the phantom day, every serial is one too many. */
    days = (serial > 60) ? serial - 1 : serial;
    --days;                     /* to a 0-based offset from 1900-01-01 */

    for (;;) {
        uint16_t len = is_leap(year) ? 366 : 365;
        if (days < len)
            break;
        days -= len;
        ++year;
    }
    for (mon = 1; mon <= 12; ++mon) {
        uint8_t len = days_in(year, mon);
        if (days < len)
            break;
        days -= len;
    }

    *y = year;
    *m = mon;
    *d = (uint8_t)days + 1;
}

static char *two(char *p, uint8_t v)
{
    *p++ = (char)('0' + v / 10);
    *p++ = (char)('0' + v % 10);
    return p;
}

uint8_t date_format(uint32_t serial, char *out)
{
    uint16_t y;
    uint8_t m, d;
    char *p = out;

    date_from_serial(serial, &y, &m, &d);

    /* ISO order, which sorts correctly as text and is unambiguous — the one
     * thing a spreadsheet showing dates from someone else's machine most
     * needs to be.
     *
     * The year as two pairs rather than four digits: cc65 charges for every
     * 16-bit divide, and this runs on every repaint of every date cell. */
    p = two(p, (uint8_t)(y / 100));
    p = two(p, (uint8_t)(y % 100));
    *p++ = '-';
    p = two(p, m);
    *p++ = '-';
    p = two(p, d);
    *p = '\0';
    return (uint8_t)(p - out);
}

/* ------------------------------------------------------------------
 * Complete and tested on the host, but not yet called by anything.
 *
 * date_to_serial waits for cells to be typed as dates, and the time pair
 * for the renderer to format them; until then a time-formatted cell shows
 * its number. Kept under test so wiring them up stays a wiring job.
 * ------------------------------------------------------------------ */
#ifdef X16S_HOST

#ifndef __CC65__
/* Host only: nothing in the program calls it, only the tests, and on
 * the machine every resident byte is contested. */
uint32_t date_to_serial(uint16_t y, uint8_t m, uint8_t d)
{
    uint32_t days = 0;
    uint16_t yy;
    uint8_t  mm;

    if (y < 1900)
        return 0;

    for (yy = 1900; yy < y; ++yy)
        days += is_leap(yy) ? 366 : 365;
    for (mm = 1; mm < m; ++mm)
        days += days_in(y, mm);
    days += d;                  /* 1900-01-01 is serial 1 */

    /* Put the phantom day back, so this is the inverse of the above. */
    if (days >= 60)
        ++days;
    return days;
}
#endif

void date_time_from_fraction(uint16_t thousandths, uint8_t *hh, uint8_t *mm,
                             uint8_t *ss)
{
    /* thousandths of a day: 86400 seconds spread over 1000 parts. Doing it
     * in seconds keeps the arithmetic exact enough that noon comes out as
     * 12:00:00 rather than 11:59:59. */
    uint32_t secs = ((uint32_t)thousandths * 86400UL + 500UL) / 1000UL;

    if (secs >= 86400UL)
        secs = 86399UL;
    *hh = (uint8_t)(secs / 3600UL);
    *mm = (uint8_t)((secs / 60UL) % 60UL);
    *ss = (uint8_t)(secs % 60UL);
}

#ifndef __CC65__
/* Host only: nothing in the program calls it, only the tests, and on
 * the machine every resident byte is contested. */
uint8_t date_format_time(uint16_t thousandths, char *out)
{
    uint8_t hh, mm, ss;
    char *p = out;

    date_time_from_fraction(thousandths, &hh, &mm, &ss);
    p = two(p, hh);
    *p++ = ':';
    p = two(p, mm);
    *p++ = ':';
    p = two(p, ss);
    *p = '\0';
    return (uint8_t)(p - out);
}
#endif

#endif /* X16S_HOST */
