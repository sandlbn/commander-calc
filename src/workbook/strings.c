#include "strings.h"
#include "strings_priv.h"
#include "../platform/banked_ram.h"
#include "../platform/bankmem.h"
#include <string.h>

/* The id table cannot be one allocation: 8192 ids of 4 bytes is 32 KB and a
 * block may never cross a bank. Four chunks of 2048 ids fit exactly one
 * bank each. */

/* Record layout in banked RAM: refs, length, then the bytes. */

/* Golden RAM, like every other block of workbook state -- see
 * cfg/x16sheet.cfg. strpool_init() writes all of it. */
X16S_GOLDEN_BEGIN
handle_t strp_chunk[STR_CHUNKS];
X16S_ZP_END
/* Zero page with free_head below: the next id is read on every add and
 * written whenever the pool grows, and the bytes come out of a different
 * memory area from the resident image. */
X16S_ZP_BEGIN
uint16_t strp_next_id;        /* highest id ever handed out */
X16S_ZP_END
X16S_GOLDEN_BEGIN
/* The free-list head (0 = empty) is read and written on every add and
 * every unref. Zero page rather than golden RAM with the rest: it is a
 * different memory area from the resident image, so two bytes here are two
 * bytes of that image given back as well as a shorter instruction at each
 * use. */
X16S_ZP_BEGIN
static uint16_t free_head;
X16S_ZP_END
uint16_t strp_live;
X16S_GOLDEN_END

err_t strpool_init(void)
{
    strpool_reset();
    return ERR_OK;
}

void strpool_reset(void)
{
    memset(strp_chunk, 0, sizeof strp_chunk);
    strp_next_id = 0;
    free_head = 0;
    strp_live = 0;
}

/* Where the handle for `id` lives. Returns 0 if the chunk is absent. */
static handle_t slot_of(uint16_t id, uint16_t *off)
{
    uint16_t i = (uint16_t)(id - 1);

    *off = (uint16_t)((i & (STR_CHUNK_IDS - 1)) * 4);
    return strp_chunk[i >> 11];
}

handle_t strp_rec_of(uint16_t id)
{
    uint16_t off;
    handle_t c, h;

    if (id == STR_NONE || id > strp_next_id)
        return H_NULL;
    c = slot_of(id, &off);
    if (c == H_NULL)
        return H_NULL;

    /* A free slot holds the next free id, not a handle (see alloc_id()).
     * Records are allocated from bank 1 upwards, so bank 0 means this id is
     * on the free list rather than in use -- without the test, a freed id
     * reads as a live record in bank 0, which belongs to the KERNAL. */
    h = bank_peek32(c, off);
    return H_BANK(h) == 0 ? H_NULL : h;
}

void strp_set_rec(uint16_t id, handle_t rec)
{
    uint16_t off;
    handle_t c = slot_of(id, &off);

    if (c != H_NULL)
        bank_poke32(c, off, rec);
}

/* Claim an id, reusing a freed one when possible. */
static err_t alloc_id(uint16_t *out)
{
    uint16_t id;

    if (free_head != STR_NONE) {
        uint16_t off;
        handle_t c;
        id = free_head;
        c = slot_of(id, &off);
        /* The free list threads through the id table itself: a free slot
         * holds the next free id rather than a record handle. */
        free_head = (uint16_t)bank_peek32(c, off);
        *out = id;
        return ERR_OK;
    }

    if (strp_next_id >= X16S_MAX_STRINGS)
        return ERR_LIMIT;

    id = (uint16_t)(strp_next_id + 1);
    {
        uint16_t ci = (uint16_t)((id - 1) >> 11);
        if (strp_chunk[ci] == H_NULL) {
            strp_chunk[ci] = bank_calloc(STR_CHUNK_BYTES);
            if (strp_chunk[ci] == H_NULL)
                return ERR_NOMEM;
        }
    }
    strp_next_id = id;
    *out = id;
    return ERR_OK;
}

#ifdef X16S_POOL_VERIFY
/* Walk every id and count the ones that cannot be right: a live record must
 * sit in a bank the allocator hands out, and cannot be longer than the pool
 * will store.
 *
 * A diagnostic for the host build and the test harness only -- pool
 * corruption is otherwise silent, and surfaces much later as a saved file
 * that will not load. */
uint16_t strpool_verify(void)
{
    uint16_t id, bad = 0;
    uint16_t banks = bankmem_bank_count();

    for (id = 1; id <= strp_next_id; ++id) {
        handle_t rec = strp_rec_of(id);
        uint8_t  bank;

        if (rec == H_NULL)
            continue;               /* free, or never used */

        bank = H_BANK(rec);
        if (bank == 0 || bank >= banks) {
            ++bad;
            continue;
        }
        if (bank_peek16(rec, REC_LEN) > X16S_MAX_TEXT_LEN)
            ++bad;
    }
    return bad;
}
#endif /* X16S_POOL_VERIFY */

err_t strpool_add(const char *s, uint16_t len, uint16_t *id)
{
    handle_t rec;
    uint16_t new_id;
    err_t e;

    if (len > X16S_MAX_TEXT_LEN)
        return ERR_LIMIT;

    e = alloc_id(&new_id);
    if (e != ERR_OK)
        return e;

    rec = bank_alloc((uint16_t)(REC_TEXT + len));
    if (rec == H_NULL) {
        /* Hand the id straight back rather than burning it. */
        uint16_t off;
        handle_t c = slot_of(new_id, &off);
        bank_poke32(c, off, (handle_t)free_head);
        free_head = new_id;
        return ERR_NOMEM;
    }

    bank_poke16(rec, REC_REFS, 1);
    bank_poke16(rec, REC_LEN, len);
    if (len)
        bank_write(rec, REC_TEXT, s, len);

    strp_set_rec(new_id, rec);
    ++strp_live;
    *id = new_id;
    return ERR_OK;
}

/* strpool_ref() is in strpool_ref_ovl.c: the worksheet parser is the only
 * thing that takes a second reference, and it lives in OVL_SHEET. */

void strpool_unref(uint16_t id)
{
    handle_t rec = strp_rec_of(id);
    uint16_t refs;

    if (rec == H_NULL)
        return;

    refs = bank_peek16(rec, REC_REFS);
    if (refs > 1) {
        bank_poke16(rec, REC_REFS, (uint16_t)(refs - 1));
        return;
    }

    bank_free(rec);
    --strp_live;

    /* Return the id to the free list by overwriting its table slot. */
    {
        uint16_t off;
        handle_t c = slot_of(id, &off);
        bank_poke32(c, off, (handle_t)free_head);
        free_head = id;
    }
}

uint16_t strpool_len(uint16_t id)
{
    handle_t rec = strp_rec_of(id);

    return rec == H_NULL ? 0 : bank_peek16(rec, REC_LEN);
}

uint16_t strpool_get(uint16_t id, char *buf, uint16_t max)
{
    handle_t rec = strp_rec_of(id);
    uint16_t len, n;

    if (max == 0)
        return 0;
    if (rec == H_NULL) {
        buf[0] = '\0';
        return 0;
    }

    len = bank_peek16(rec, REC_LEN);
    n = len < (uint16_t)(max - 1) ? len : (uint16_t)(max - 1);
    if (n)
        bank_read(rec, REC_TEXT, buf, n);
    buf[n] = '\0';
    return len;
}




/* strpool_read() is not here: the .X16S writer is its only caller and it
 * lives in that overlay now, in strpool_read_ovl.c. See strings_priv.h. */

