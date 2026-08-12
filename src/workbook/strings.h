/* strings.h — the workbook's text, addressed by id.
 *
 * Cells hold a 16-bit string id, not a handle. That costs one indirection
 * and buys two things: the id is what goes into the .X16S file, and it is
 * what XLSX's sharedStrings table already looks like, so the importer maps
 * onto it directly.
 *
 * Strings are reference counted. A cell that is overwritten or deleted
 * releases its string, and the last release frees it — without that, editing
 * one cell repeatedly would walk through the 8192-string budget and never
 * give any of it back.
 *
 * There is deliberately no de-duplication yet. Adding a hash index later is
 * an internal change: it alters neither this API nor the file format, and
 * the place it actually earns its keep is XLSX import (step 12), where the
 * same label repeats across thousands of rows.
 */
#ifndef X16S_STRINGS_H
#define X16S_STRINGS_H

#include "../util/errors.h"

#define STR_NONE  0             /* id 0 is "no string", never allocated */

err_t    strpool_init(void);
void     strpool_reset(void);

/* Store `len` bytes and return a fresh id with a reference count of 1.
 * ERR_LIMIT when the id space is full, ERR_NOMEM when the heap is. */
err_t    strpool_add(const char *s, uint16_t len, uint16_t *id);

void     strpool_ref(uint16_t id);
void     strpool_unref(uint16_t id);

/* Copy into `buf` (NUL-terminated, truncated to `max` including the NUL).
 * Returns the full stored length, which may exceed what was copied. */
uint16_t strpool_get(uint16_t id, char *buf, uint16_t max);
uint16_t strpool_len(uint16_t id);

/* Live strings. A variable for the same reason as strp_next_id. */
extern uint16_t strp_live;
#define strpool_count() strp_live

/* --- for the file writer and reader ---
 * Saving walks 1..strpool_max_id() and writes the live ones with their ids.
 * Loading puts each back at the id it had, so cell records need no
 * remapping. Ids that were free when the file was written stay unused until
 * the pool is reset — a bounded waste, and much safer than renumbering every
 * cell reference on the way out. */
/* A variable, not an accessor: it is two bytes and the call was most of
 * the cost. Same reasoning as wb_sheet_n. */
extern uint16_t strp_next_id;
#define strpool_max_id() strp_next_id
/* Whether an id is in use. A macro over the lookup, because the only
 * caller is the .X16S writer in its own overlay -- as a resident function
 * it was 25 bytes nothing resident ever called. */
handle_t strp_rec_of(uint16_t id);
#define strpool_alive(id) (strp_rec_of(id) != H_NULL)

#ifdef X16S_POOL_VERIFY
/* How many records are impossible: 0 is a healthy pool. Compiled only into
 * the tests and the on-target harness — see strings.c. */
uint16_t strpool_verify(void);
#endif
err_t    strpool_load(uint16_t id, const char *s, uint16_t len);

/* Chunked transfer, so the file layer never needs a buffer the size of the
 * longest possible string. cc65 caps a function's locals well below the
 * 1024-byte text limit, so a whole string simply cannot sit on the stack. */
uint16_t strpool_read(uint16_t id, uint16_t off, void *buf, uint16_t len);
err_t    strpool_load_begin(uint16_t id, uint16_t len);
void     strpool_load_chunk(uint16_t id, uint16_t off,
                            const void *src, uint16_t len);

#endif /* X16S_STRINGS_H */
