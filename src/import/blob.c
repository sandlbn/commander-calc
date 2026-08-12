#include "blob.h"
#include "../platform/banked_ram.h"
#include "../platform/bankmem.h"
#include <string.h>

/* An overlay that only consumes a blob — the XLSX parsers read one and never
 * build one — leaves out everything that makes one. That is most of this
 * file, and the difference between its copy fitting and not. */
#ifndef BLOB_READER_ONLY

/* Whole banks, one at a time, in whatever order the allocator gives them. */
err_t blob_alloc(blob_t *b, uint32_t bytes)
{
    uint8_t want, i;
    handle_t h;

    blob_free(b);

    if (bytes == 0 || bytes > BLOB_MAX_BYTES)
        return ERR_LIMIT;

    want = (uint8_t)((bytes + BANK_SIZE - 1) / BANK_SIZE);

    for (i = 0; i < want; ++i) {
        h = bank_alloc(BANK_SIZE);
        if (h == H_NULL) {
            /* Give back what was taken. A partial reservation held onto
             * after a refusal is a leak that only shows up on the next
             * import. */
            b->banks = i;
            blob_free(b);
            return ERR_LIMIT;
        }
        b->bank[i] = H_BANK(h);
    }

    b->banks = want;
    b->len = 0;
    b->pos = 0;
    return ERR_OK;
}

void blob_free(blob_t *b)
{
    /* Really free them. An import stages several parts in turn — workbook,
     * relationships, shared strings, then every worksheet — and the cells
     * being built live in the same heap, so resetting it between phases is
     * not available. Leaking a blob's banks runs a real workbook out of
     * memory partway through, which is exactly what a large one would do
     * and a small one would not. */
    uint8_t i;

    for (i = 0; i < b->banks; ++i)
        bank_free_whole(b->bank[i]);
    memset(b, 0, sizeof *b);
}

void blob_reset_write(blob_t *b) { b->len = 0; }

#endif /* !BLOB_READER_ONLY */

void blob_reset_read(blob_t *b)  { b->pos = 0; }

/* Bank and offset from a position. The bank size is a power of two, so this
 * is a shift and a mask rather than a divide — which matters, because it
 * happens on every chunk of every entry. */
#define BLOB_BANK(b, p) ((b)->bank[(uint8_t)((p) >> 13)])
#define BLOB_OFF(p)     ((uint16_t)((p) & 0x1FFF))

#ifndef BLOB_READER_ONLY

uint8_t blob_write(blob_t *b, const void *data, uint16_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t cap = (uint32_t)b->banks * BANK_SIZE;

    while (len) {
        uint16_t off, room, n;

        if (b->len >= cap)
            return 0;                   /* full */

        off  = BLOB_OFF(b->len);
        room = (uint16_t)(BANK_SIZE - off);
        n    = len < room ? len : room; /* never cross a bank in one copy */

        bankmem_write(BLOB_BANK(b, b->len), off, p, n);
        b->len += n;
        p      += n;
        len     = (uint16_t)(len - n);
    }
    return 1;
}

#endif /* !BLOB_READER_ONLY */

uint16_t blob_read(blob_t *b, void *out, uint16_t len)
{
    uint8_t *p = (uint8_t *)out;
    uint16_t done = 0;

    while (len) {
        uint16_t off, room, n;
        uint32_t left = b->len - b->pos;

        if (b->pos >= b->len)
            break;

        off  = BLOB_OFF(b->pos);
        room = (uint16_t)(BANK_SIZE - off);
        n    = len < room ? len : room;
        if ((uint32_t)n > left)
            n = (uint16_t)left;

        bankmem_read(BLOB_BANK(b, b->pos), off, p, n);
        b->pos += n;
        p      += n;
        done    = (uint16_t)(done + n);
        len     = (uint16_t)(len - n);
    }
    return done;
}

uint16_t blob_feed(void *ctx, void *out, uint16_t len)
{
    return blob_read((blob_t *)ctx, out, len);
}
