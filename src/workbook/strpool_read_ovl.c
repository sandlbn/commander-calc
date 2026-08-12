/* strpool_read_ovl.c — reading a slice of a pool record.
 *
 * The .X16S writer is the only thing that reads a string a piece at a time:
 * it streams each record into the file rather than holding one in a
 * buffer. Everything else wants the whole string and calls strpool_get().
 *
 * So this lives in OVL_X16S_SAVE with the writer, and not in resident
 * memory, which is the same trade strpool_load.c makes on the reading side
 * -- and at the time it moved, resident memory was over budget by exactly
 * the sort of margin one function like this covers.
 *
 * See strings_priv.h for why this may touch the pool's insides.
 */
#include "strings_priv.h"
#include "../platform/banked_ram.h"

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY11")
#  pragma rodata-name (push, "OVL11RO")
#  pragma bss-name (push, "OVL11BSS")
#endif

uint16_t strpool_read(uint16_t id, uint16_t off, void *buf, uint16_t len)
{
    handle_t rec = strp_rec_of(id);
    uint16_t have;

    if (rec == H_NULL)
        return 0;
    have = bank_peek16(rec, REC_LEN);
    if (off >= have)
        return 0;
    if ((uint16_t)(off + len) > have)
        len = (uint16_t)(have - off);
    if (len)
        bank_read(rec, (uint16_t)(REC_TEXT + off), buf, len);
    return len;
}

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
