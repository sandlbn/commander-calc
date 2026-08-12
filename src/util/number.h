/* number.h — the spreadsheet number type.
 *
 * snum_t is a 5-byte packed Microsoft Binary Format float: an 8-bit exponent
 * biased by 128, then a 32-bit mantissa whose top bit carries the sign and
 * whose leading 1 is implicit. Exponent 0 means the value is zero. That is
 * exactly the format the X16 ROM math library works in, so arithmetic costs
 * a jump into ROM bank 4 rather than a software float library.
 *
 * Roughly 9 significant decimal digits, magnitude up to ~1.7e38.
 *
 *   9.81  ->  84 1C F5 C2 8F
 *   5.0   ->  83 20 00 00 00
 *   1.0   ->  81 00 00 00 00
 *
 * This is binary floating point, not decimal. 0.1 + 0.2 rounds the way it
 * does everywhere else. Number formats round on display, so money columns
 * look right, but an exact `=A1=0.3` comparison can still fail.
 *
 * WHY THE ERROR RETURNS MATTER: the ROM math library reports overflow and
 * division by zero by jumping into BASIC's error handler, which would print
 * a BASIC error and drop the user back to READY, taking their workbook with
 * it. Every entry point here checks the operands first and returns an error
 * rather than letting the ROM be asked a question it answers fatally.
 */
#ifndef X16S_NUMBER_H
#define X16S_NUMBER_H

#include "errors.h"

typedef struct { uint8_t b[5]; } snum_t;

#define SNUM_TEXT_MAX 24        /* enough for anything fout produces */

extern const snum_t snum_zero;
extern const snum_t snum_one;
extern const snum_t snum_ten;

/* --- inspection (no ROM call) ------------------------------------- */
#define snum_is_zero(a) ((a)->b[0] == 0)

int8_t snum_sign(const snum_t *a);          /* -1, 0, +1 */
int8_t snum_cmp(const snum_t *a, const snum_t *b);

/* --- arithmetic: *acc = *acc OP *b -------------------------------- */
err_t snum_add(snum_t *acc, const snum_t *b);
err_t snum_sub(snum_t *acc, const snum_t *b);
err_t snum_mul(snum_t *acc, const snum_t *b);
err_t snum_div(snum_t *acc, const snum_t *b);   /* ERR_DIVZERO if *b == 0 */
err_t snum_pow(snum_t *acc, const snum_t *e);
err_t snum_mod(snum_t *acc, const snum_t *b);
#ifndef __CC65__       /* host only -- see the definition */
err_t snum_sqrt(snum_t *acc);                   /* ERR_NUM if negative */
#endif

void snum_neg(snum_t *acc);
void snum_abs(snum_t *acc);
void snum_int(snum_t *acc);                     /* floor, as BASIC INT */
err_t snum_round(snum_t *acc, int8_t places);   /* round half away from 0 */

/* --- conversion --------------------------------------------------- */
void  snum_from_i16(snum_t *r, int16_t v);
#ifndef __CC65__       /* host only -- see the definition */
err_t snum_from_i32(snum_t *r, int32_t v);
#endif
err_t snum_to_i32(const snum_t *a, int32_t *out);
err_t snum_to_u16(const snum_t *a, uint16_t *out);

/* Parse a decimal literal: optional sign, digits, optional fraction,
 * optional E exponent. Trailing whitespace is allowed, other trailing
 * characters are not. Returns ERR_SYNTAX or ERR_NUM. */
err_t snum_parse(const char *s, snum_t *out);

/* General format, the ROM's own rendering: integers without a point,
 * scientific notation only when needed. `out` must hold SNUM_TEXT_MAX.
 * Returns the string length. */
uint8_t snum_to_text(const snum_t *a, char *out);

/* Fixed number of decimal places, always exactly `places` of them.
 * Returns the string length, or 0 if the value will not fit. */
uint8_t snum_to_fixed(const snum_t *a, uint8_t places, char *out);

#endif /* X16S_NUMBER_H */
