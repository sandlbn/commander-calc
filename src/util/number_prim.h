/* number_prim.h — the raw MBF operations, one implementation per target.
 *
 * X16  (number_x16.s)   thin wrappers around ROM bank 4, $FE00..$FE99
 * host (number_host.c)  decode to double, operate, re-encode
 *
 * These are unguarded. They assume the caller has already ruled out the
 * cases the ROM answers by jumping into BASIC's error handler: division by
 * zero, overflow, square root of a negative. number.c is that caller, and
 * nothing else should call into this header.
 *
 * Host and X16 results agree to MBF precision but are not bit-identical:
 * the ROM rounds on a 40-bit accumulator with a rounding byte, the host
 * rounds a 53-bit double back down to 32 mantissa bits. Tests compare at
 * nine significant digits, never bitwise.
 */
#ifndef X16S_NUMBER_PRIM_H
#define X16S_NUMBER_PRIM_H

#include "number.h"

void snprim_from_i16(snum_t *r, int16_t v);

/* 0 if equal, 1 if *a > *b, 255 if *a < *b — the ROM's own encoding. */
uint8_t snprim_cmp(const snum_t *a, const snum_t *b);

void snprim_add(snum_t *acc, const snum_t *b);
void snprim_sub(snum_t *acc, const snum_t *b);
void snprim_mul(snum_t *acc, const snum_t *b);
void snprim_div(snum_t *acc, const snum_t *b);
void snprim_pow(snum_t *acc, const snum_t *e);
void snprim_sqrt(snum_t *acc);

void snprim_neg(snum_t *acc);
void snprim_abs(snum_t *acc);
void snprim_int(snum_t *acc);

/* Signed 32-bit truncation. The caller must have checked the exponent:
 * |*a| < 2^31, i.e. a->b[0] <= 159. */
int32_t snprim_to_i32(const snum_t *a);

/* ROM general-format rendering into `out` (SNUM_TEXT_MAX bytes).
 * Returns the length. */
uint8_t snprim_to_text(const snum_t *a, char *out);

#ifdef X16S_TARGET_X16
/* Count how many bytes of cc65's zero page ($22..$7F) a full arithmetic
 * sequence through the ROM math library disturbs. Should be 0: ZPMATH lives
 * at $A9..$D3. Returns the number of changed bytes. */
uint8_t snprim_zp_probe(void);
#endif

#endif /* X16S_NUMBER_PRIM_H */
