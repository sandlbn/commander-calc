#include "bankmem.h"
#include <string.h>

/* The single-value accessors live in bankmem_acc_x16.s: they are on the
 * path of every cell the renderer draws, and in C each carried cc65's
 * 16-bit pointer helpers for what assembly does in six instructions.
 * What is left here is memcpy and memset with a bank store in front, where
 * the library routines are already better than hand-written code. */

#define RAM_BANK   (*(volatile uint8_t *)0x0000)
#define WINDOW     ((uint8_t *)0xA000)

/* Set by bankmem_bank_count() on first call; see bankmem_x16.s. */
extern uint8_t bankmem_memtop_banks(void);

uint16_t bankmem_bank_count(void)
{
    static uint16_t cached;

    if (cached == 0) {
        uint8_t n = bankmem_memtop_banks();
        /* MEMTOP reports 2 MB as 0, which is really 256. */
        cached = (n == 0) ? 256u : (uint16_t)n;
    }
    return cached;
}

void bankmem_read(uint8_t bank, uint16_t off, void *dst, uint16_t len)
{
    RAM_BANK = bank;
    memcpy(dst, WINDOW + off, len);
}

void bankmem_write(uint8_t bank, uint16_t off, const void *src, uint16_t len)
{
    RAM_BANK = bank;
    memcpy(WINDOW + off, src, len);
}

void bankmem_set(uint8_t bank, uint16_t off, uint8_t value, uint16_t len)
{
    RAM_BANK = bank;
    memset(WINDOW + off, value, len);
}

