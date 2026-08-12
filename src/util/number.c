/* number.c — guards, parsing and formatting on top of the raw MBF ops.
 *
 * The guards exist because the ROM math library handles overflow and
 * division by zero by jumping into BASIC's error handler. There is no return
 * from that: BASIC prints ?DIVISION BY ZERO ERROR and drops to READY, and
 * the user's unsaved workbook is gone. So every operation that could reach
 * one of those paths is range-checked here first, from the exponent bytes
 * alone, before the ROM is asked anything.
 *
 * The exponent arithmetic is deliberately conservative. A normalised MBF
 * mantissa lies in [0.5, 1), so a product's exponent is ea+eb-128 or
 * ea+eb-129. Rejecting ea+eb >= 384 can therefore refuse one value that
 * would just barely have fit, at the very top of the range (~1.7e38). That
 * is the right trade: a spurious #NUM! on an absurd number costs nothing,
 * and a crash costs the workbook.
 */
#include "number.h"
#include "number_prim.h"
#include <string.h>

const snum_t snum_zero = { { 0x00, 0x00, 0x00, 0x00, 0x00 } };
const snum_t snum_one  = { { 0x81, 0x00, 0x00, 0x00, 0x00 } };
const snum_t snum_ten  = { { 0x84, 0x20, 0x00, 0x00, 0x00 } };

/* 0.5, for rounding half away from zero. The exponent byte is biased by
 * 129, so one less than snum_one's 0x81 halves it. */
static const snum_t snum_half = { { 0x80, 0x00, 0x00, 0x00, 0x00 } };

/* Above this exponent a value can no longer be a 32-bit integer:
 * |v| < 2^(exp-128), so exp <= 159 guarantees |v| < 2^31. */
#define EXP_FITS_I32 159
#define EXP_FITS_U16 144

int8_t snum_sign(const snum_t *a)
{
    if (a->b[0] == 0)
        return 0;
    return (a->b[1] & 0x80) ? -1 : 1;
}

int8_t snum_cmp(const snum_t *a, const snum_t *b)
{
    uint8_t r = snprim_cmp(a, b);

    if (r == 0)   return 0;
    return (r == 1) ? 1 : -1;
}

/* --- arithmetic --------------------------------------------------- */

err_t snum_add(snum_t *acc, const snum_t *b)
{
    /* A sum can carry into one more exponent, no further. */
    if (acc->b[0] == 255 || b->b[0] == 255)
        return ERR_NUM;
    snprim_add(acc, b);
    return ERR_OK;
}

err_t snum_sub(snum_t *acc, const snum_t *b)
{
    if (acc->b[0] == 255 || b->b[0] == 255)
        return ERR_NUM;
    snprim_sub(acc, b);
    return ERR_OK;
}

err_t snum_mul(snum_t *acc, const snum_t *b)
{
    if (acc->b[0] == 0 || b->b[0] == 0) {
        *acc = snum_zero;
        return ERR_OK;
    }
    if ((uint16_t)acc->b[0] + (uint16_t)b->b[0] >= 384)
        return ERR_NUM;
    snprim_mul(acc, b);
    return ERR_OK;
}

err_t snum_div(snum_t *acc, const snum_t *b)
{
    if (b->b[0] == 0)
        return ERR_DIVZERO;
    if (acc->b[0] == 0)
        return ERR_OK;                       /* 0 / x is 0 */
    /* Quotient exponent is ea-eb+128 or +129. */
    if ((int16_t)acc->b[0] - (int16_t)b->b[0] >= 126)
        return ERR_NUM;
    snprim_div(acc, b);
    return ERR_OK;
}

#ifndef __CC65__
/* Host only: nothing in the program calls it, only the tests, and on
 * the machine every resident byte is contested. */
err_t snum_sqrt(snum_t *acc)
{
    if (snum_sign(acc) < 0)
        return ERR_NUM;
    if (acc->b[0] == 0)
        return ERR_OK;
    snprim_sqrt(acc);
    return ERR_OK;
}
#endif

err_t snum_pow(snum_t *acc, const snum_t *e)
{
    int8_t base_sign = snum_sign(acc);

    if (e->b[0] == 0) {                      /* x^0 == 1, including 0^0 */
        *acc = snum_one;
        return ERR_OK;
    }
    if (base_sign == 0) {
        if (snum_sign(e) < 0)
            return ERR_DIVZERO;              /* 0 to a negative power */
        return ERR_OK;                       /* 0^positive == 0 */
    }
    if (base_sign < 0) {
        /* The ROM computes x^y as exp(y*log(x)), which traps for a negative
         * base. Only integer exponents have a real answer anyway. */
        snum_t t;                       /* cc65 cannot initialise a
                                         * struct from an expression */
        t = *e;
        snprim_int(&t);
        if (snum_cmp(&t, e) != 0)
            return ERR_NUM;
    }
    snprim_pow(acc, e);
    return ERR_OK;
}

err_t snum_mod(snum_t *acc, const snum_t *b)
{
    /* Excel's MOD: a - b*INT(a/b), so the result takes the divisor's sign. */
    snum_t q;
    err_t e;

    if (b->b[0] == 0)
        return ERR_DIVZERO;

    q = *acc;
    e = snum_div(&q, b);
    if (e != ERR_OK)
        return e;
    snprim_int(&q);
    e = snum_mul(&q, b);
    if (e != ERR_OK)
        return e;
    return snum_sub(acc, &q);
}

void snum_neg(snum_t *acc)
{
    if (acc->b[0] != 0)
        snprim_neg(acc);
}

void snum_abs(snum_t *acc)
{
    if (acc->b[0] != 0)
        acc->b[1] &= 0x7F;               /* the sign is just a mantissa bit */
}

void snum_int(snum_t *acc)
{
    snprim_int(acc);
}

/* --- conversion --------------------------------------------------- */

void snum_from_i16(snum_t *r, int16_t v)
{
    snprim_from_i16(r, v);
}

#ifdef __CC65__
/* No caller in the resident program: the only user today is a test, and the
 * first real one will be the XLSX importer, which reads integers out of XML.
 * Parked in that overlay rather than deleted, so the API and its test stay. */
#  pragma code-name (push, "OVERLAY7")
#endif

#ifndef __CC65__
/* Host only: nothing in the program calls it, only the tests, and on
 * the machine every resident byte is contested. */
err_t snum_from_i32(snum_t *r, int32_t v)
{
    /* givayf only takes 16 bits, so build the value as hi*65536 + lo.
     * Splitting on a power of two keeps both halves exact. */
    snum_t lo;
    int32_t h = v / 65536L;
    int32_t l = v - h * 65536L;
    static const snum_t k65536 = { { 0x91, 0x00, 0x00, 0x00, 0x00 } };

    snprim_from_i16(r, (int16_t)h);
    snprim_mul(r, &k65536);
    snprim_from_i16(&lo, (int16_t)l);
    /* l is in (-65536, 65536) and may not fit int16_t; split again. */
    if (l > 32767L || l < -32768L) {
        snum_t half;
        snprim_from_i16(&lo, (int16_t)(l / 2));
        snprim_from_i16(&half, (int16_t)(l - (l / 2)));
        snprim_add(&lo, &half);
    }
    snprim_add(r, &lo);
    return ERR_OK;
}
#endif

#ifdef __CC65__
#  pragma code-name (pop)
#endif

#ifdef __CC65__
/* The only caller anywhere is chart.c, which is entirely OVERLAY15 -- so
 * this is 144 bytes the resident image was carrying for one overlay. Same
 * bargain as snum_from_i32 and OVERLAY7 above. */
#  pragma code-name (push, "OVERLAY15")
#endif

err_t snum_to_u16(const snum_t *a, uint16_t *out)
{
    int32_t v;
    err_t e;

    if (a->b[0] != 0 && a->b[0] > EXP_FITS_U16)
        return ERR_NUM;
    e = snum_to_i32(a, &v);
    if (e != ERR_OK)
        return e;
    if (v < 0 || v > 65535L)
        return ERR_NUM;
    *out = (uint16_t)v;
    return ERR_OK;
}

#ifdef __CC65__
#  pragma code-name (pop)
#endif

err_t snum_to_i32(const snum_t *a, int32_t *out)
{
    if (a->b[0] == 0) {
        *out = 0;
        return ERR_OK;
    }
    if (a->b[0] > EXP_FITS_I32)
        return ERR_NUM;
    *out = snprim_to_i32(a);
    return ERR_OK;
}


/* --- parsing ------------------------------------------------------ */

#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')

/* Beyond this many digits the mantissa cannot hold more information, so
 * further digits only move the decimal exponent. */
#define PARSE_MAX_DIGITS 11

err_t snum_parse(const char *s, snum_t *out)
{
    snum_t acc;
    snum_t d;
    uint8_t neg = 0, any = 0, kept = 0;
    int16_t scale = 0;             /* value = acc * 10^-scale */
    err_t e;

    acc = snum_zero;

    while (*s == ' ' || *s == '\t')
        ++s;

    if (*s == '+' || *s == '-') {
        neg = (*s == '-');
        ++s;
    }

    /* The invariant throughout is  value == acc * 10^-scale. */
    while (IS_DIGIT(*s)) {
        any = 1;
        if (kept < PARSE_MAX_DIGITS) {
            snprim_mul(&acc, &snum_ten);
            snprim_from_i16(&d, (int16_t)(*s - '0'));
            snprim_add(&acc, &d);
            ++kept;
        } else {
            /* Out of mantissa. The digit still counts for magnitude, so the
             * value is ten times larger while acc stands still. */
            --scale;
            if (scale < -300) return ERR_NUM;
        }
        ++s;
    }

    if (*s == '.') {
        ++s;
        while (IS_DIGIT(*s)) {
            any = 1;
            if (kept < PARSE_MAX_DIGITS) {
                snprim_mul(&acc, &snum_ten);
                snprim_from_i16(&d, (int16_t)(*s - '0'));
                snprim_add(&acc, &d);
                ++kept;
                ++scale;       /* acc grew tenfold, the value did not */
            }
            /* Digits past the mantissa's reach are simply dropped: they
             * change neither acc nor the magnitude. */
            ++s;
        }
    }

    if (!any)
        return ERR_SYNTAX;

    if (*s == 'e' || *s == 'E') {
        int16_t ev = 0;
        uint8_t eneg = 0, edig = 0;
        ++s;
        if (*s == '+' || *s == '-') {
            eneg = (*s == '-');
            ++s;
        }
        while (IS_DIGIT(*s)) {
            edig = 1;
            if (ev < 4000)
                ev = (int16_t)(ev * 10 + (*s - '0'));
            ++s;
        }
        if (!edig)
            return ERR_SYNTAX;
        scale = (int16_t)(scale - (eneg ? -ev : ev));
    }

    while (*s == ' ' || *s == '\t')
        ++s;
    if (*s != '\0')
        return ERR_SYNTAX;

    /* Apply the decimal exponent one factor of ten at a time. Each step is
     * guarded, so 1E999 reports #NUM! instead of trapping in the ROM. */
    while (scale > 0) {
        e = snum_div(&acc, &snum_ten);
        if (e != ERR_OK)
            return (acc.b[0] == 0) ? ERR_OK : e;
        if (acc.b[0] == 0)
            break;                 /* underflowed to zero; done */
        --scale;
    }
    while (scale < 0) {
        e = snum_mul(&acc, &snum_ten);
        if (e != ERR_OK)
            return e;
        ++scale;
    }

    if (neg)
        snum_neg(&acc);
    *out = acc;
    return ERR_OK;
}

/* --- formatting --------------------------------------------------- */

uint8_t snum_to_text(const snum_t *a, char *out)
{
    return snprim_to_text(a, out);
}

/* Multiply or divide by ten `n` times: positive scales up, negative down. */
static err_t scale10(snum_t *v, int8_t n)
{
    err_t e = ERR_OK;

    while (n > 0 && e == ERR_OK) {
        e = snum_mul(v, &snum_ten);
        --n;
    }
    while (n < 0 && e == ERR_OK) {
        e = snum_div(v, &snum_ten);
        ++n;
    }
    return e;
}

err_t snum_round(snum_t *acc, int8_t places)
{
    snum_t v;
    int8_t sign = snum_sign(acc);
    err_t e;

    if (sign == 0)
        return ERR_OK;

    /* Assigned, not initialised: cc65 will not build a struct from a
     * dereferenced pointer in a declaration. */
    v = *acc;
    snum_abs(&v);

    /* Half away from zero, which is what a spreadsheet user expects and
     * what Excel's ROUND does -- not banker's rounding. The value is
     * positive by here, so adding 0.5 before flooring is enough. */
    e = scale10(&v, places);
    if (e == ERR_OK)
        e = snum_add(&v, &snum_half);
    if (e == ERR_OK) {
        snprim_int(&v);
        e = scale10(&v, (int8_t)-places);
    }
    if (e != ERR_OK)
        return e;                   /* `acc` is left as it was */

    if (sign < 0)
        snum_neg(&v);
    *acc = v;
    return ERR_OK;
}

uint8_t snum_to_fixed(const snum_t *a, uint8_t places, char *out)
{
    snum_t v;
    int32_t iv;
    char digits[12];
    uint8_t nd = 0, n = 0, i;
    int8_t sign = snum_sign(a);
    uint8_t p;

    v = *a;

    if (places > 9)
        places = 9;

    snum_abs(&v);
    /* Shift the wanted decimals above the point, then round to an integer. */
    for (p = 0; p < places; ++p) {
        if (snum_mul(&v, &snum_ten) != ERR_OK)
            return 0;
    }
    /* NOT snum_round(&v, 0): `v` is already positive and already scaled,
     * so every loop in it would run zero times and all that is left is
     * this. That call was the only thing keeping 536 bytes of rounding in
     * the resident image for a program whose other caller is in an
     * overlay. */
    if (snum_add(&v, &snum_half) != ERR_OK)
        return 0;
    snprim_int(&v);
    if (snum_to_i32(&v, &iv) != ERR_OK)
        return 0;

    if (iv == 0) {
        digits[nd++] = '0';
    } else {
        while (iv > 0 && nd < sizeof digits) {
            digits[nd++] = (char)('0' + (int)(iv % 10));
            iv /= 10;
        }
    }
    /* Ensure there is a digit for every decimal place plus one before. */
    while (nd <= places)
        digits[nd++] = '0';

    if (sign < 0)
        out[n++] = '-';
    for (i = nd; i > places; --i)
        out[n++] = digits[i - 1];
    if (places) {
        out[n++] = '.';
        for (i = places; i > 0; --i)
            out[n++] = digits[i - 1];
    }
    out[n] = '\0';
    return n;
}
