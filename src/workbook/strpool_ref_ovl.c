/* strpool_ref_ovl.c — taking a second reference to a pool string.
 *
 * One caller: the .xlsx worksheet parser, when two cells share a string
 * from the shared-string table. Every other owner takes its reference from
 * strpool_add() and returns it through strpool_unref(), both resident.
 * This sits in OVL_SHEET with its caller instead.
 *
 * See strings_priv.h for why this may touch the pool's insides.
 */
#include "strings_priv.h"
#include "../platform/banked_ram.h"

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY10")
#  pragma rodata-name (push, "OVL10RO")
#  pragma bss-name (push, "OVL10BSS")
#endif

void strpool_ref(uint16_t id)
{
    handle_t rec = strp_rec_of(id);

    if (rec != H_NULL)
        bank_poke16(rec, REC_REFS, (uint16_t)(bank_peek16(rec, REC_REFS) + 1));
}

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
