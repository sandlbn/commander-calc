#include "inflate.h"
#include "../platform/banked_ram.h"
#include "../platform/bankmem.h"
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY8")
#  pragma rodata-name (push, "OVL8RO")
#  pragma bss-name (push, "OVL8BSS")
#endif

/* Scratch for table construction: 320 code lengths, which is larger than
 * cc65 allows on the stack and far larger than the resident memory left.
 * It lives in the overlay's own bss, in the unused tail of the overlay area.
 *
 * Static rather than per-stream because only one stream is ever being set up
 * at a time — the tables are built, then decoding runs from them. */
/* IN BANKED RAM, not in the overlay, which has no room for it.
 *
 * 320 bytes of scratch, read and written only while building the tables at
 * the head of a block -- a few hundred operations against the 92,340
 * single-bit reads that decoding a 118 KB worksheet performs. So a banked
 * access per entry is affordable here, where it would not be in the
 * decoder. count[] and symbol[], which every symbol touches, stay put. */
static handle_t code_lengths;

#define CL_GET(i)     bank_peek(code_lengths, (uint16_t)(i))
#define CL_PUT(i, v)  bank_poke(code_lengths, (uint16_t)(i), (uint8_t)(v))
#define CL_BYTES      (288 + 32)

/* Decoder states, in the order a block moves through them. */
#define ST_HEADER  0            /* read the next block header  */
#define ST_STORED  1
#define ST_CODES   2            /* decoding literals and matches */
#define ST_DONE    3

/* --- bit input, least-significant bit first ------------------------ */

static uint8_t next_byte(inflate_t *s)
{
    if (s->in_pos == s->in_len) {
        s->in_len = s->feed(s->ctx, s->in, INF_IN_BUF);
        s->in_pos = 0;
        if (s->in_len == 0) {
            s->err = ERR_EOF;
            return 0;
        }
    }
    return s->in[s->in_pos++];
}

/* DEFLATE packs codes LSB-first within a byte, and Huffman codes MSB-first
 * within the code — the two orders are not a mistake, they are the format. */
/* The mask for an n-bit read, looked up rather than computed.
 *
 * bits() ran ((uint32_t)1 << need) - 1 on every call -- a 32-bit shift by a
 * VARIABLE amount and a 32-bit subtract, both of which cc65 turns into
 * runtime helper calls that loop. Inflating a 118 KB worksheet makes
 * 108,202 of those calls. DEFLATE never reads more than 13 bits at once,
 * so the whole table is 28 bytes of the overlay's rodata. */
static const uint16_t bitmask[14] = {
    0x0000, 0x0001, 0x0003, 0x0007, 0x000F, 0x001F, 0x003F, 0x007F,
    0x00FF, 0x01FF, 0x03FF, 0x07FF, 0x0FFF, 0x1FFF
};

static uint16_t bits(inflate_t *s, uint8_t need)
{
    uint16_t v;

    while (s->bitcnt < need) {
        if (s->err != ERR_OK)
            return 0;
        s->bitbuf |= (uint32_t)next_byte(s) << s->bitcnt;
        s->bitcnt = (uint8_t)(s->bitcnt + 8);
    }
    v = (uint16_t)s->bitbuf & bitmask[need];
    s->bitbuf >>= need;
    s->bitcnt = (uint8_t)(s->bitcnt - need);
    return v;
}

/* --- Huffman ------------------------------------------------------- */

/* Build a canonical code from a list of code lengths. This is the whole of
 * the RFC's table construction: codes of the same length are consecutive,
 * and each length starts where the previous one left off, doubled. */
static err_t build(uint16_t *count, uint16_t *symbol,
                   uint16_t base, uint16_t n)
{
    uint16_t offs[16];
    uint16_t sym;
    uint8_t  len;

    for (len = 0; len < 16; ++len)
        count[len] = 0;
    for (sym = 0; sym < n; ++sym)
        ++count[CL_GET(base + sym)];

    /* Length 0 means "symbol unused", not a zero-bit code. */
    count[0] = 0;

    offs[1] = 0;
    for (len = 1; len < 15; ++len)
        offs[len + 1] = (uint16_t)(offs[len] + count[len]);

    for (sym = 0; sym < n; ++sym) {
        uint8_t l = CL_GET(base + sym);
        if (l)
            symbol[offs[l]++] = sym;
    }

    return ERR_OK;
}

/* Walk the tree one bit at a time. `code` accumulates the bits read, `first`
 * is the first code of this length and `index` the first symbol index; when
 * code - first is inside this length's range, the symbol is found. */
static int16_t decode(inflate_t *s, const uint16_t *count,
                      const uint16_t *symbol)
{
    int16_t code = 0, first = 0, index = 0;
    uint8_t len;

    for (len = 1; len < 16; ++len) {
        code |= (int16_t)bits(s, 1);
        if (s->err != ERR_OK)
            return -1;
        {
            int16_t n = (int16_t)count[len];
            if (code - first < n)
                return (int16_t)symbol[index + (code - first)];
            index = (int16_t)(index + n);
            first = (int16_t)((first + n) << 1);
            code <<= 1;
        }
    }
    s->err = ERR_BADFORMAT;
    return -1;
}

/* The fixed tables of RFC 1951 section 3.2.6. Built rather than stored:
 * the lengths are describable in eight lines, and a stored table would be
 * 288 bytes of overlay for something used only by small blocks. */
static void fixed_tables(inflate_t *s)
{
    uint16_t i;

    for (i = 0; i < 144; ++i) CL_PUT(i, 8);
    for (; i < 256; ++i)      CL_PUT(i, 9);
    for (; i < 280; ++i)      CL_PUT(i, 7);
    for (; i < 288; ++i)      CL_PUT(i, 8);
    build(s->lit.count, s->lit.symbol, 0, 288);

    for (i = 0; i < 30; ++i)  CL_PUT(i, 5);
    build(s->dist.count, s->dist.symbol, 0, 30);
}

/* --- dynamic block header ------------------------------------------ */

/* The order the code-length code lengths appear in, RFC 1951 3.2.7. It is
 * shuffled so the common lengths come first and trailing zeros can be
 * omitted. */
static const uint8_t clc_order[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
};

static err_t dynamic_tables(inflate_t *s)
{
    /* The code-length alphabet borrows the distance table: 19 symbols fit
     * in its 30, and it is not needed until afterwards. */
    huffdist_t *clc = &s->dist;
    uint16_t nlen, ndist, ncode, i = 0;

    nlen  = (uint16_t)(bits(s, 5) + 257);
    ndist = (uint16_t)(bits(s, 5) + 1);
    ncode = (uint16_t)(bits(s, 4) + 4);
    if (s->err != ERR_OK)
        return s->err;
    if (nlen > 286 || ndist > 30)
        return (s->err = ERR_BADFORMAT);

    for (i = 0; i < ncode; ++i)
        CL_PUT(clc_order[i], bits(s, 3));
    for (; i < 19; ++i)
        CL_PUT(clc_order[i], 0);
    build(clc->count, clc->symbol, 0, 19);

    /* Then the literal and distance lengths, themselves Huffman-coded, with
     * three run-length escapes. */
    i = 0;
    while (i < nlen + ndist) {
        int16_t sym = decode(s, clc->count, clc->symbol);
        uint16_t rep;
        uint8_t  val = 0;

        if (sym < 0)
            return s->err;

        if (sym < 16) {
            CL_PUT(i++, sym);
            continue;
        }
        if (sym == 16) {
            if (i == 0)
                return (s->err = ERR_BADFORMAT);
            val = CL_GET(i - 1);        /* repeat the previous length */
            rep = (uint16_t)(3 + bits(s, 2));
        } else if (sym == 17) {
            rep = (uint16_t)(3 + bits(s, 3));
        } else {
            rep = (uint16_t)(11 + bits(s, 7));
        }
        if (i + rep > nlen + ndist)
            return (s->err = ERR_BADFORMAT);
        while (rep--)
            CL_PUT(i++, val);
    }

    build(s->lit.count, s->lit.symbol, 0, nlen);
    build(s->dist.count, s->dist.symbol, nlen, ndist);
    return s->err;
}

/* --- length and distance tables ------------------------------------ */

static const uint16_t len_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43,
    51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t len_extra[29] = {
    0,0,0,0,0,0,0,0, 1,1,1,1, 2,2,2,2, 3,3,3,3, 4,4,4,4, 5,5,5,5, 0
};
static const uint16_t dist_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385,
    513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static const uint8_t dist_extra[30] = {
    0,0,0,0, 1,1, 2,2, 3,3, 4,4, 5,5, 6,6, 7,7, 8,8, 9,9,
    10,10, 11,11, 12,12, 13,13
};

/* --- the sliding window, in banked RAM ------------------------------ */

/* The window is 32 KB across four banks. Its position maps to a bank and an
 * offset directly, because the bank size and the window size are both powers
 * of two — no division. */
static void win_put(inflate_t *s, uint8_t byte)
{
    uint8_t  bank = s->win_bank[s->win_pos >> 13];
    uint16_t off  = (uint16_t)(s->win_pos & 0x1FFF);

    bankmem_poke(bank, off, byte);
    s->win_pos = (uint16_t)((s->win_pos + 1) & (INF_WIN_SIZE - 1));
    if (s->win_pos == 0)
        s->win_wrapped = 1;
}

static uint8_t win_get(inflate_t *s, uint16_t distance)
{
    uint16_t p = (uint16_t)((s->win_pos - distance) & (INF_WIN_SIZE - 1));

    return bankmem_peek(s->win_bank[p >> 13], (uint16_t)(p & 0x1FFF));
}

/* --- lifecycle ------------------------------------------------------ */

err_t inflate_init(inflate_t *s, inflate_feed_t feed, void *ctx)
{
    uint8_t i;

    memset(s, 0, sizeof *s);
    s->feed = feed;
    s->ctx  = ctx;
    s->err  = ERR_OK;

    /* Four whole banks, in whatever order the allocator gives them. */
    for (i = 0; i < 4; ++i) {
        handle_t h = bank_alloc(BANK_SIZE);
        if (h == H_NULL) {
            s->win_banks = i;
            inflate_free(s);            /* give back the partial set */
            return (s->err = ERR_NOMEM);
        }
        s->win_bank[i] = H_BANK(h);
    }
    s->win_banks = 4;

    code_lengths = bank_alloc(CL_BYTES);
    if (code_lengths == H_NULL) {
        inflate_free(s);
        return (s->err = ERR_NOMEM);
    }
    return ERR_OK;
}

void inflate_free(inflate_t *s)
{
    uint8_t i;

    for (i = 0; i < s->win_banks; ++i)
        bank_free_whole(s->win_bank[i]);
    s->win_banks = 0;

    bank_free(code_lengths);
    code_lengths = H_NULL;
}

/* --- the main loop --------------------------------------------------- */

uint16_t inflate_read(inflate_t *s, void *buf, uint16_t len)
{
    uint8_t *out = (uint8_t *)buf;
    uint16_t n = 0;

    while (n < len && s->err == ERR_OK && s->state != ST_DONE) {

        /* Finish a match that ran past the end of the last call. */
        if (s->copy_len) {
            uint8_t b = win_get(s, s->copy_dist);
            win_put(s, b);
            out[n++] = b;
            --s->copy_len;
            continue;
        }

        switch (s->state) {

        case ST_HEADER: {
            uint8_t type;
            s->last_block = (uint8_t)bits(s, 1);
            type = (uint8_t)bits(s, 2);
            if (s->err != ERR_OK)
                break;

            if (type == 0) {
                uint16_t l, nl;
                s->bitbuf = 0;          /* stored blocks are byte-aligned */
                s->bitcnt = 0;
                l  = (uint16_t)(next_byte(s) | (next_byte(s) << 8));
                nl = (uint16_t)(next_byte(s) | (next_byte(s) << 8));
                if ((uint16_t)~l != nl) {
                    s->err = ERR_BADFORMAT;
                    break;
                }
                s->block_left = l;
                s->state = ST_STORED;
            } else if (type == 1) {
                fixed_tables(s);
                s->state = ST_CODES;
            } else if (type == 2) {
                if (dynamic_tables(s) != ERR_OK)
                    break;
                s->state = ST_CODES;
            } else {
                s->err = ERR_BADFORMAT; /* type 3 is reserved */
            }
            break;
        }

        case ST_STORED: {
            uint8_t b;
            if (s->block_left == 0) {
                s->state = s->last_block ? ST_DONE : ST_HEADER;
                break;
            }
            b = next_byte(s);
            if (s->err != ERR_OK)
                break;
            win_put(s, b);
            out[n++] = b;
            --s->block_left;
            break;
        }

        case ST_CODES: {
            int16_t sym = decode(s, s->lit.count, s->lit.symbol);

            if (sym < 0)
                break;
            if (sym < 256) {
                win_put(s, (uint8_t)sym);
                out[n++] = (uint8_t)sym;
            } else if (sym == 256) {
                s->state = s->last_block ? ST_DONE : ST_HEADER;
            } else {
                uint16_t idx = (uint16_t)(sym - 257);
                int16_t  dsym;
                if (idx >= 29) {
                    s->err = ERR_BADFORMAT;
                    break;
                }
                s->copy_len = (uint16_t)(len_base[idx]
                                         + bits(s, len_extra[idx]));
                dsym = decode(s, s->dist.count, s->dist.symbol);
                if (dsym < 0 || dsym >= 30) {
                    if (s->err == ERR_OK)
                        s->err = ERR_BADFORMAT;
                    break;
                }
                s->copy_dist = (uint16_t)(dist_base[dsym]
                                          + bits(s, dist_extra[dsym]));
                /* A distance reaching further back than we have written is
                 * a corrupt stream, not a wrap. */
                if (!s->win_wrapped && s->copy_dist > s->win_pos) {
                    s->err = ERR_BADFORMAT;
                    s->copy_len = 0;
                }
            }
            break;
        }
        }
    }

    if (s->state == ST_DONE)
        s->done = 1;
    return n;
}

/* inflate_selftest lived here. It decompressed a built-in fixture whose
 * back-references spanned the whole 32 KB window, to prove the banked-RAM
 * window worked on real hardware — something the host suite cannot do.
 *
 * `make xlsx` now does that better: it runs the decoder on the machine
 * against a 52 KB worksheet out of a real archive, exercising the same
 * window with data nobody chose to suit it. The fixture and its driver were
 * costing overlay space for a weaker guarantee.
 */

/* The one live decoder state, used by inflate_blob.c. An inflate_t is far
 * too large for cc65's stack, and only one stream is ever being decoded. */
inflate_t inflate_shared_state;

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
