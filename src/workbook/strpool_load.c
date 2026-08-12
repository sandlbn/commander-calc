/* strpool_load.c — building pool records with ids the caller chose.
 *
 * Reading a .X16S means recreating the pool exactly as saved, ids and all,
 * rather than interning text and taking whatever id comes back. Nothing
 * else needs that, so this lives in OVL_FILEIO beside its one caller.
 *
 * See strings_priv.h for why this may touch the pool's insides.
 */
#include "strings_priv.h"
#include "../platform/banked_ram.h"

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY2")
#  pragma rodata-name (push, "OVL2RO")
#endif

err_t strpool_load_begin(uint16_t id, uint16_t len)
{
    handle_t rec;
    uint16_t ci;

    if (id == STR_NONE || id > X16S_MAX_STRINGS)
        return ERR_BADFORMAT;
    if (len > X16S_MAX_TEXT_LEN)
        return ERR_LIMIT;

    ci = (uint16_t)((id - 1) >> 11);
    if (strp_chunk[ci] == H_NULL) {
        strp_chunk[ci] = bank_calloc(STR_CHUNK_BYTES);
        if (strp_chunk[ci] == H_NULL)
            return ERR_NOMEM;
    }

    rec = bank_alloc((uint16_t)(REC_TEXT + len));
    if (rec == H_NULL)
        return ERR_NOMEM;
    bank_poke16(rec, REC_REFS, 1);
    bank_poke16(rec, REC_LEN, len);

    if (id > strp_next_id)
        strp_next_id = id;
    strp_set_rec(id, rec);
    ++strp_live;
    return ERR_OK;
}

void strpool_load_chunk(uint16_t id, uint16_t off, const void *src,
                        uint16_t len)
{
    handle_t rec = strp_rec_of(id);

    if (rec != H_NULL && len)
        bank_write(rec, (uint16_t)(REC_TEXT + off), src, len);
}

err_t strpool_load(uint16_t id, const char *s, uint16_t len)
{
    err_t e = strpool_load_begin(id, len);

    if (e != ERR_OK)
        return e;
    strpool_load_chunk(id, 0, s, len);
    return ERR_OK;
}

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
