/* banked_ram.h — the workbook heap.
 *
 * A size-class allocator over banked RAM. Blocks are addressed by handle_t
 * and never by pointer, so a bank switch cannot invalidate anything a caller
 * is holding.
 *
 * Blocks never cross a bank boundary, which is what lets bankmem_* skip
 * range checks on the fast path. The cost is the tail of a bank being
 * abandoned when the next request will not fit; bank_stats() reports it.
 */
#ifndef X16S_BANKED_RAM_H
#define X16S_BANKED_RAM_H

#include "../util/errors.h"

/* Largest single allocation: one whole bank. */
#define BANK_MAX_ALLOC  BANK_SIZE

/* Zero golden RAM ($0400-$07FF), which the startup code does not.
 *
 * Call it once, before anything else, from every entry point -- main() and
 * the on-target harness both. It lives here, rather than next to either
 * main(), so that the two cannot drift: a harness that skipped it would be
 * testing a different machine from the one that ships, and the whole point
 * of the harness is that it is not.
 *
 * A no-op off the X16, where X16S_GOLDEN_BEGIN expands to nothing and the
 * statics it guards are ordinary bss. */
void golden_init(void);

/* Prepare the heap over banks `first`..`first+count-1`.
 * `first` must be >= 1; bank 0 belongs to the KERNAL. Passing count == 0
 * means "all banks this machine reports". */
err_t bank_heap_init(uint8_t first, uint16_t count);

/* Allocate `size` bytes, rounded up to a size class. Returns H_NULL if the
 * heap is exhausted or size is 0 or above BANK_MAX_ALLOC. The contents are
 * not cleared; use bank_calloc() when they must be. */
handle_t bank_alloc(uint16_t size);
handle_t bank_calloc(uint16_t size);

/* Return a block to its free list. Passing H_NULL is a no-op. The size class
 * travels in the handle, so no size argument can be got wrong. */
void bank_free(handle_t h);

/* Free a whole-bank allocation given only its bank number.
 *
 * For callers that hold banks rather than handles — the import blobs take
 * consecutive whole banks and track the first one and a count, because
 * keeping 48 handles would cost more than the thing they describe. */
void bank_free_whole(uint8_t bank);

/* Usable bytes actually reserved for a handle — its size class, which is
 * >= the requested size. Growth code uses this to avoid reallocating when
 * the existing block already has room. */
uint16_t bank_block_size(handle_t h);

/* Copy `size` bytes into a larger block and free the old one. Returns H_NULL
 * and leaves `h` untouched if there is no room. */
handle_t bank_realloc(handle_t h, uint16_t old_size, uint16_t new_size);

/* --- accessors: these are the normal way to touch a block --- */
void     bank_read(handle_t h, uint16_t off, void *dst, uint16_t len);
void     bank_write(handle_t h, uint16_t off, const void *src, uint16_t len);
uint8_t  bank_peek(handle_t h, uint16_t off);
void     bank_poke(handle_t h, uint16_t off, uint8_t v);
uint16_t bank_peek16(handle_t h, uint16_t off);
void     bank_poke16(handle_t h, uint16_t off, uint16_t v);
handle_t bank_peek32(handle_t h, uint16_t off);
void     bank_poke32(handle_t h, uint16_t off, handle_t v);

/* --- reporting, for the status bar and for tests --- */
typedef struct {
    uint32_t total;      /* bytes in all banks given to the heap        */
    uint32_t used;       /* bytes handed out and not yet freed          */
    uint32_t recycled;   /* bytes sitting on free lists                 */
    uint32_t wasted;     /* bank tails too small for the next request   */
    uint16_t banks;
} bank_stats_t;

#ifndef __CC65__       /* host only -- see the definition */
void     bank_stats(bank_stats_t *out);
#endif
uint32_t bank_bytes_free(void);

#endif /* X16S_BANKED_RAM_H */
