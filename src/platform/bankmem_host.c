/* bankmem_host.c — banked RAM simulated as one flat array.
 *
 * Deliberately strict where the X16 would be silent: an out-of-range bank or
 * an access that runs off the end of a bank aborts instead of corrupting a
 * neighbour. On real hardware those bugs are near-invisible; here they fail
 * on the first test that commits them.
 */
#include "bankmem.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint8_t *mem;
static uint16_t banks;

int bankmem_host_init(uint16_t n)
{
    bankmem_host_free();
    mem = calloc((size_t)n, BANK_SIZE);
    if (!mem)
        return 0;
    banks = n;
    return 1;
}

void bankmem_host_free(void)
{
    free(mem);
    mem = 0;
    banks = 0;
}

uint16_t bankmem_bank_count(void) { return banks; }

static uint8_t *at(uint8_t bank, uint16_t off, uint16_t len)
{
    if (!mem) {
        fprintf(stderr, "bankmem: used before bankmem_host_init()\n");
        abort();
    }
    if (bank >= banks) {
        fprintf(stderr, "bankmem: bank %u out of range (have %u)\n",
                bank, banks);
        abort();
    }
    if ((uint32_t)off + len > BANK_SIZE) {
        fprintf(stderr, "bankmem: access at %u+%u crosses the end of bank %u\n",
                off, len, bank);
        abort();
    }
    return mem + (size_t)bank * BANK_SIZE + off;
}

void bankmem_read(uint8_t bank, uint16_t off, void *dst, uint16_t len)
{
    memcpy(dst, at(bank, off, len), len);
}

void bankmem_write(uint8_t bank, uint16_t off, const void *src, uint16_t len)
{
    memcpy(at(bank, off, len), src, len);
}

void bankmem_set(uint8_t bank, uint16_t off, uint8_t value, uint16_t len)
{
    memset(at(bank, off, len), value, len);
}

uint8_t bankmem_peek(uint8_t bank, uint16_t off)
{
    return *at(bank, off, 1);
}

void bankmem_poke(uint8_t bank, uint16_t off, uint8_t value)
{
    *at(bank, off, 1) = value;
}

uint16_t bankmem_peek16(uint8_t bank, uint16_t off)
{
    const uint8_t *p = at(bank, off, 2);
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

void bankmem_poke16(uint8_t bank, uint16_t off, uint16_t value)
{
    uint8_t *p = at(bank, off, 2);
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

handle_t bankmem_peek32(uint8_t bank, uint16_t off)
{
    const uint8_t *p = at(bank, off, 4);
    return (handle_t)p[0]
         | ((handle_t)p[1] << 8)
         | ((handle_t)p[2] << 16)
         | ((handle_t)p[3] << 24);
}

void bankmem_poke32(uint8_t bank, uint16_t off, handle_t value)
{
    uint8_t *p = at(bank, off, 4);
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

void *bankmem_map(uint8_t bank, uint16_t off, uint16_t len)
{
    return at(bank, off, len);
}
