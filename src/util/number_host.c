/* number_host.c — MBF arithmetic on the host, via double.
 *
 * Every result is re-encoded to MBF, which rounds it back to a 32-bit
 * mantissa. That keeps host results within MBF precision of the X16's, so
 * the portable code above can be trusted after passing here — but the two
 * are not bit-identical, because the ROM rounds on a 40-bit accumulator with
 * a separate rounding byte. Tests compare rendered digits, not bytes.
 */
#include "number_prim.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define TWO32 4294967296.0

double snhost_to_double(const snum_t *a)
{
    uint32_t m;
    int exp, neg;
    double v;

    if (a->b[0] == 0)
        return 0.0;

    exp = (int)a->b[0] - 128;
    m = ((uint32_t)a->b[1] << 24) | ((uint32_t)a->b[2] << 16)
      | ((uint32_t)a->b[3] << 8)  | (uint32_t)a->b[4];

    neg = (m & 0x80000000u) != 0;
    m |= 0x80000000u;              /* the leading 1 the sign bit displaced */

    v = ldexp((double)m / TWO32, exp);
    return neg ? -v : v;
}

void snhost_from_double(double v, snum_t *r)
{
    int exp, neg = 0;
    double frac;
    unsigned long long mm;
    uint32_t m;

    if (v == 0.0 || !isfinite(v)) {
        memset(r, 0, sizeof *r);
        return;
    }
    if (v < 0) { neg = 1; v = -v; }

    frac = frexp(v, &exp);                     /* 0.5 <= frac < 1 */
    mm = (unsigned long long)(frac * TWO32 + 0.5);
    if (mm >= 0x100000000ull) {                /* rounded up to 1.0 */
        mm >>= 1;
        ++exp;
    }
    m = (uint32_t)mm;

    exp += 128;
    if (exp > 255) {                           /* overflow: saturate */
        r->b[0] = 255;
        m = 0xFFFFFFFFu;
    } else if (exp <= 0) {                     /* underflow: zero */
        memset(r, 0, sizeof *r);
        return;
    } else {
        r->b[0] = (uint8_t)exp;
    }

    m &= 0x7FFFFFFFu;                          /* sign replaces leading 1 */
    if (neg)
        m |= 0x80000000u;

    r->b[1] = (uint8_t)(m >> 24);
    r->b[2] = (uint8_t)(m >> 16);
    r->b[3] = (uint8_t)(m >> 8);
    r->b[4] = (uint8_t)m;
}

/* --- primitives --------------------------------------------------- */

void snprim_from_i16(snum_t *r, int16_t v)
{
    snhost_from_double((double)v, r);
}

uint8_t snprim_cmp(const snum_t *a, const snum_t *b)
{
    double x = snhost_to_double(a), y = snhost_to_double(b);

    if (x == y) return 0;
    return x > y ? 1 : 255;
}

#define BINOP(name, expr)                                       \
    void name(snum_t *acc, const snum_t *b)                     \
    {                                                           \
        double x = snhost_to_double(acc);                       \
        double y = snhost_to_double(b);                         \
        snhost_from_double(expr, acc);                          \
    }

BINOP(snprim_add, x + y)
BINOP(snprim_sub, x - y)
BINOP(snprim_mul, x * y)
BINOP(snprim_div, x / y)
BINOP(snprim_pow, pow(x, y))

void snprim_sqrt(snum_t *acc)
{
    snhost_from_double(sqrt(snhost_to_double(acc)), acc);
}

void snprim_neg(snum_t *acc)
{
    snhost_from_double(-snhost_to_double(acc), acc);
}

void snprim_abs(snum_t *acc)
{
    snhost_from_double(fabs(snhost_to_double(acc)), acc);
}

void snprim_int(snum_t *acc)
{
    snhost_from_double(floor(snhost_to_double(acc)), acc);
}

int32_t snprim_to_i32(const snum_t *a)
{
    /* The ROM's qint truncates toward zero. */
    double v = snhost_to_double(a);
    return (int32_t)(v < 0 ? ceil(v) : floor(v));
}

/* Mimic the ROM's FOUT: up to 9 significant digits, no trailing zeros, and
 * scientific notation outside a middle range.
 *
 * FOUT itself reserves the CBM sign column and returns " 42" for a positive
 * value; snprim_to_text on the X16 strips that space, so neither backend
 * produces one and both agree. */
uint8_t snprim_to_text(const snum_t *a, char *out)
{
    double v = snhost_to_double(a);
    char tmp[40];
    int n, i;

    if (v == 0.0) {
        out[0] = '0';
        out[1] = '\0';
        return 1;
    }

    {
        double av = fabs(v);
        if (av >= 1e9 || av < 1e-2) {
            snprintf(tmp, sizeof tmp, "%.8E", v);
            /* Trim the mantissa's trailing zeros: 1.20000000E+15 -> 1.2E+15 */
            {
                char *e = strchr(tmp, 'E');
                char *p = e - 1;
                while (p > tmp && *p == '0') --p;
                if (*p == '.') --p;
                memmove(p + 1, e, strlen(e) + 1);
            }
        } else {
            snprintf(tmp, sizeof tmp, "%.9G", v);
        }
    }

    /* CBM renders magnitudes below 1 with no leading zero: .4, not 0.4.
     * Matching that here is what keeps host results comparable with the
     * X16's, since every display test compares rendered text. */
    if (tmp[0] == '0' && tmp[1] == '.')
        memmove(tmp, tmp + 1, strlen(tmp));
    else if (tmp[0] == '-' && tmp[1] == '0' && tmp[2] == '.')
        memmove(tmp + 1, tmp + 2, strlen(tmp + 1));

    n = (int)strlen(tmp);
    if (n > SNUM_TEXT_MAX - 1)
        n = SNUM_TEXT_MAX - 1;
    for (i = 0; i < n; ++i)
        out[i] = tmp[i];
    out[n] = '\0';
    return (uint8_t)n;
}
