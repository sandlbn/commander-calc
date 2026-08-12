/* strings_priv.h — the string pool's insides, for the loader only.
 *
 * strpool_load_begin() and strpool_load_chunk() build a record with an id
 * the caller chose rather than one the pool hands out, which is what
 * reading a .X16S needs and nothing else does. They are 563 bytes, and the
 * only thing that calls them is the .X16S reader in OVL_FILEIO — so they
 * live there, in strpool_load.c, and the resident core gets those bytes
 * back.
 *
 * That means the pool's state has to be reachable from outside strings.c.
 * It is exposed here rather than made public: the invariant is that only
 * strings.c and strpool_load.c include this, and the loader touches the
 * state in exactly the ways the pool's own allocation path does.
 *
 * The state itself stays resident. It is the code that moved, not the data
 * — a second copy of `chunk` or `next_id` in an overlay would be a second
 * string pool that disagreed with the first.
 */
#ifndef X16S_STRINGS_PRIV_H
#define X16S_STRINGS_PRIV_H

#include "strings.h"

/* One chunk of ids per bank: a banked block may never cross a bank. */
#define STR_CHUNK_IDS   2048
#define STR_CHUNKS      (X16S_MAX_STRINGS / STR_CHUNK_IDS)
#define STR_CHUNK_BYTES (STR_CHUNK_IDS * 4)

/* Offsets within a record. */
#define REC_REFS  0
#define REC_LEN   2
#define REC_TEXT  4

extern handle_t strp_chunk[STR_CHUNKS];
extern uint16_t strp_next_id;       /* highest id ever handed out */
extern uint16_t strp_live;

handle_t strp_rec_of(uint16_t id);
void     strp_set_rec(uint16_t id, handle_t rec);

#endif /* X16S_STRINGS_PRIV_H */
