#include "banked_ram.h"
#include "bankmem.h"
#include <string.h>

/* Size classes are powers of two from 8 to 8192. Eleven of them fit in the
 * four class bits of a handle with room to spare.
 *
 * Powers of two suit this workload: the row cell arrays are the dominant
 * allocation and they grow by doubling, so they land exactly on a class
 * rather than just over one. */
#define CLASS_COUNT   11
#define CLASS_MIN_LOG 3                       /* 1 << 3 == 8 */
#define CLASS_SIZE(c) ((uint16_t)1 << ((c) + CLASS_MIN_LOG))

/* See banked_ram.h. The literal bounds are the GOLDEN area in
 * cfg/x16sheet.cfg; two constants rather than a linker import because the
 * import costs more code than the memset, and each end names the other. */
void golden_init(void)
{
#ifdef __CC65__
    /* zpbss_x16.s -- the linker's own symbols, which C cannot name. */
    extern uint8_t *const zpbss_start;
    extern const uint16_t zpbss_size;

    memset((void *)0x0400, 0, 0x0400);

    /* The zero-page variables too. Neither area is touched by crt0, which
     * zeroes the default BSS segment and nothing else, so both hold
     * whatever the last program left until this runs. */
    memset(zpbss_start, 0, zpbss_size);
#endif
}

/* The whole allocator's bookkeeping, in golden RAM rather than bss -- see
 * cfg/x16sheet.cfg. bank_heap_init() sets every one of these, and runs after
 * golden_init() clears the area, so they are zero before they are anything
 * else. */
X16S_GOLDEN_BEGIN
static handle_t freelist[CLASS_COUNT];

static uint8_t  first_bank;
static uint16_t bank_count;
/* The bump pointer, in zero page: every allocation reads and writes both,
 * and zero page is a separate memory area, so the three bytes come out of
 * nothing the resident image was using. */
X16S_ZP_BEGIN
static uint8_t  cur_bank;      /* bump pointer: bank ... */
static uint16_t cur_off;       /* ... and offset within it */
X16S_ZP_END

static uint32_t stat_used;
static uint32_t stat_recycled;
static uint32_t stat_wasted;
X16S_GOLDEN_END

/* Smallest class that holds `size`, or CLASS_COUNT if it does not fit. */
static uint8_t class_for(uint16_t size)
{
    uint8_t c = 0;
    uint16_t s = 1 << CLASS_MIN_LOG;

    while (c < CLASS_COUNT) {
        if (size <= s)
            return c;
        /* The largest class is 8192, which is BANK_SIZE and would overflow
         * a uint16_t if doubled again. Stop before that happens. */
        if (c == CLASS_COUNT - 1)
            break;
        s <<= 1;
        ++c;
    }
    return CLASS_COUNT;
}

err_t bank_heap_init(uint8_t first, uint16_t count)
{
    uint16_t have;

    if (first < 1)
        return ERR_VALUE;

    have = bankmem_bank_count();
    if (have <= first)
        return ERR_NOMEM;

    if (count == 0 || (uint32_t)first + count > have)
        count = have - first;

    memset(freelist, 0, sizeof freelist);
    first_bank  = first;
    bank_count  = count;
    cur_bank    = first;
    cur_off     = 0;
    stat_used = stat_recycled = stat_wasted = 0;

    return ERR_OK;
}

handle_t bank_alloc(uint16_t size)
{
    uint8_t  c;
    uint16_t csize;
    handle_t h;

    if (size == 0)
        return H_NULL;

    c = class_for(size);
    if (c >= CLASS_COUNT)
        return H_NULL;                  /* larger than one bank */
    csize = CLASS_SIZE(c);

    /* A recycled block of the right class costs one read. */
    if (freelist[c] != H_NULL) {
        h = freelist[c];
        freelist[c] = bankmem_peek32(H_BANK(h), H_OFF(h));
        stat_recycled -= csize;
        stat_used     += csize;
        return H_WITH_CLASS(h, c);
    }

    /* Otherwise bump. A block may not straddle two banks, so a tail too
     * small for this class is abandoned. */
    if ((uint32_t)cur_off + csize > BANK_SIZE) {
        stat_wasted += BANK_SIZE - cur_off;
        if ((uint16_t)(cur_bank - first_bank) + 1 >= bank_count)
            return H_NULL;              /* heap exhausted */
        ++cur_bank;
        cur_off = 0;
    }

    h = H_MAKE(cur_bank, cur_off);
    cur_off   += csize;
    stat_used += csize;
    return H_WITH_CLASS(h, c);
}

handle_t bank_calloc(uint16_t size)
{
    handle_t h = bank_alloc(size);

    if (h != H_NULL)
        bankmem_set(H_BANK(h), H_OFF(h), 0, bank_block_size(h));
    return h;
}

void bank_free(handle_t h)
{
    uint8_t c;

    if (h == H_NULL)
        return;

    c = H_CLASS(h);
    if (c == 0 || c > CLASS_COUNT)
        return;                          /* untracked or corrupt: leak it */
    --c;

    /* The free list link lives in the block itself, so recycling costs no
     * main RAM at all. */
    bankmem_poke32(H_BANK(h), H_OFF(h), freelist[c]);
    freelist[c] = H_MAKE(H_BANK(h), H_OFF(h));

    stat_used     -= CLASS_SIZE(c);
    stat_recycled += CLASS_SIZE(c);
}

/* The class index of a full-bank allocation: CLASS_SIZE(c) is 1 << (c+3),
 * so 8192 is class 10. Derived rather than written as a literal so it stays
 * correct if the class scheme changes. */
#define FULL_BANK_CLASS (CLASS_COUNT - 1)

void bank_free_whole(uint8_t bank)
{
    bank_free(H_WITH_CLASS(H_MAKE(bank, 0), FULL_BANK_CLASS));
}

uint16_t bank_block_size(handle_t h)
{
    uint8_t c = H_CLASS(h);

    if (h == H_NULL || c == 0 || c > CLASS_COUNT)
        return 0;
    return CLASS_SIZE(c - 1);
}

handle_t bank_realloc(handle_t h, uint16_t old_size, uint16_t new_size)
{
    handle_t n;
    uint16_t copy;

    /* Growing inside the existing class is free. */
    if (h != H_NULL && new_size <= bank_block_size(h))
        return h;

    n = bank_alloc(new_size);
    if (n == H_NULL)
        return H_NULL;

    if (h != H_NULL) {
        copy = old_size < new_size ? old_size : new_size;
        while (copy) {
            uint8_t buf[64];
            uint16_t chunk = copy < sizeof buf ? copy : sizeof buf;
            uint16_t at = (old_size < new_size ? old_size : new_size) - copy;
            bankmem_read(H_BANK(h), (uint16_t)(H_OFF(h) + at), buf, chunk);
            bankmem_write(H_BANK(n), (uint16_t)(H_OFF(n) + at), buf, chunk);
            copy -= chunk;
        }
        bank_free(h);
    }
    return n;
}

/* --- accessors ---------------------------------------------------- */

void bank_read(handle_t h, uint16_t off, void *dst, uint16_t len)
{
    bankmem_read(H_BANK(h), (uint16_t)(H_OFF(h) + off), dst, len);
}

void bank_write(handle_t h, uint16_t off, const void *src, uint16_t len)
{
    bankmem_write(H_BANK(h), (uint16_t)(H_OFF(h) + off), src, len);
}

uint8_t bank_peek(handle_t h, uint16_t off)
{
    return bankmem_peek(H_BANK(h), (uint16_t)(H_OFF(h) + off));
}

void bank_poke(handle_t h, uint16_t off, uint8_t v)
{
    bankmem_poke(H_BANK(h), (uint16_t)(H_OFF(h) + off), v);
}

uint16_t bank_peek16(handle_t h, uint16_t off)
{
    return bankmem_peek16(H_BANK(h), (uint16_t)(H_OFF(h) + off));
}

void bank_poke16(handle_t h, uint16_t off, uint16_t v)
{
    bankmem_poke16(H_BANK(h), (uint16_t)(H_OFF(h) + off), v);
}

handle_t bank_peek32(handle_t h, uint16_t off)
{
    return bankmem_peek32(H_BANK(h), (uint16_t)(H_OFF(h) + off));
}

void bank_poke32(handle_t h, uint16_t off, handle_t v)
{
    bankmem_poke32(H_BANK(h), (uint16_t)(H_OFF(h) + off), v);
}

/* --- reporting ---------------------------------------------------- */

#ifndef __CC65__
/* Host only: nothing in the program calls it, only the tests, and on
 * the machine every resident byte is contested. */
void bank_stats(bank_stats_t *out)
{
    out->total    = (uint32_t)bank_count * BANK_SIZE;
    out->used     = stat_used;
    out->recycled = stat_recycled;
    out->wasted   = stat_wasted;
    out->banks    = bank_count;
}
#endif

uint32_t bank_bytes_free(void)
{
    /* Everything never handed out, plus everything on a free list. Bank
     * tails already written off are not counted: they are gone. */
    uint32_t banks_left = (uint32_t)(bank_count - (cur_bank - first_bank) - 1);

    return banks_left * BANK_SIZE + (BANK_SIZE - cur_off) + stat_recycled;
}
