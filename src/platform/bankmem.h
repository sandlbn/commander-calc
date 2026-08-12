/* bankmem.h — raw access to banked RAM. The layer below the allocator.
 *
 * On the X16 this drives the $A000..$BFFF window and the bank register at
 * $00. On the host it is a flat array, so everything above this file is
 * testable natively.
 *
 * Every call takes an explicit (bank, offset) rather than a handle: the
 * allocator owns handle encoding, this layer owns the hardware.
 *
 * No access may cross a bank boundary. The allocator guarantees that by
 * never placing a block across the end of a bank; these functions do not
 * re-check it on the X16, where the cost would be paid on every cell read.
 */
#ifndef X16S_BANKMEM_H
#define X16S_BANKMEM_H

#include "../x16sheet.h"

/* Number of usable 8 KB RAM banks, banks 1..n-1 being available to us
 * (bank 0 belongs to the KERNAL). 512 KB reports 64, 2 MB reports 256. */
uint16_t bankmem_bank_count(void);

void bankmem_read(uint8_t bank, uint16_t off, void *dst, uint16_t len);
void bankmem_write(uint8_t bank, uint16_t off, const void *src, uint16_t len);
void bankmem_set(uint8_t bank, uint16_t off, uint8_t value, uint16_t len);

uint8_t bankmem_peek(uint8_t bank, uint16_t off);
void    bankmem_poke(uint8_t bank, uint16_t off, uint8_t value);

/* Little-endian 16-bit and handle-sized accessors, used constantly by the
 * index structures. */
uint16_t bankmem_peek16(uint8_t bank, uint16_t off);
void     bankmem_poke16(uint8_t bank, uint16_t off, uint16_t value);
handle_t bankmem_peek32(uint8_t bank, uint16_t off);
void     bankmem_poke32(uint8_t bank, uint16_t off, handle_t value);

/* Map a bank into the window and return a pointer to `len` bytes at `off`.
 *
 * ONE MAPPING MAY BE LIVE AT A TIME. Any other bankmem_* call, or any call
 * into code that touches banked RAM, invalidates the pointer. Use this only
 * inside a tight sequence that does nothing else — the sorted-insert memmove
 * in cells.c is the motivating case — and prefer read/write everywhere else.
 *
 * `len` is what the host backend range-checks; the X16 backend ignores it,
 * since the allocator already guarantees a block never crosses a bank.
 */
void *bankmem_map(uint8_t bank, uint16_t off, uint16_t len);

#ifdef X16S_HOST
/* Host-only: build a simulated memory of `banks` banks, all zeroed.
 * Returns 0 on allocation failure. */
int  bankmem_host_init(uint16_t banks);
void bankmem_host_free(void);
#endif

#endif /* X16S_BANKMEM_H */
