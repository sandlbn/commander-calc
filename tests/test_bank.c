#include "test.h"
#include "../src/platform/bankmem.h"
#include "../src/platform/banked_ram.h"

/* 8 banks is enough to exercise bank rollover without a slow test. */
#define TEST_BANKS 8

static void setup(void)
{
    CHECK(bankmem_host_init(TEST_BANKS));
    CHECK_EQ(bank_heap_init(1, 0), ERR_OK);
}

static void test_handles(void)
{
    handle_t h = H_MAKE(37, 0x1234);

    CHECK_EQ(H_BANK(h), 37);
    CHECK_EQ(H_OFF(h), 0x1234);

    h = H_WITH_CLASS(h, 5);
    CHECK_EQ(H_BANK(h), 37);            /* class bits must not bleed down */
    CHECK_EQ(H_OFF(h), 0x1234);
    CHECK_EQ(H_CLASS(h), 6);            /* stored as class + 1 */

    /* The fields are byte-aligned, which is the whole point. */
    CHECK_EQ(H_MAKE(1, 0), 0x00010000UL);
    CHECK_EQ(H_BANK(H_MAKE(255, 8191)), 255);
    CHECK_EQ(H_OFF(H_MAKE(255, 8191)), 8191);

    /* The null handle must be unreachable by a real allocation, which is
     * why the heap starts at bank 1. */
    CHECK_EQ(H_MAKE(0, 0), H_NULL);
}

static void test_alloc_basics(void)
{
    handle_t a, b;

    setup();

    CHECK_EQ(bank_alloc(0), H_NULL);
    CHECK_EQ(bank_alloc(BANK_MAX_ALLOC + 1), H_NULL);

    a = bank_alloc(1);
    CHECK(a != H_NULL);
    CHECK_EQ(bank_block_size(a), 8);    /* rounded up to the smallest class */

    b = bank_alloc(100);
    CHECK(b != H_NULL);
    CHECK_EQ(bank_block_size(b), 128);
    CHECK(a != b);

    /* A full-bank allocation must still be one block. */
    CHECK_EQ(bank_block_size(bank_alloc(BANK_SIZE)), BANK_SIZE);

    bankmem_host_free();
}

static void test_read_write(void)
{
    handle_t h;
    uint8_t out[16];
    static const uint8_t in[16] = { 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16 };
    uint8_t i;

    setup();

    h = bank_alloc(16);
    bank_write(h, 0, in, sizeof in);
    bank_read(h, 0, out, sizeof out);
    for (i = 0; i < 16; ++i)
        CHECK_EQ(out[i], in[i]);

    bank_poke(h, 3, 0xAB);
    CHECK_EQ(bank_peek(h, 3), 0xAB);

    bank_poke16(h, 4, 0xBEEF);
    CHECK_EQ(bank_peek16(h, 4), 0xBEEF);
    CHECK_EQ(bank_peek(h, 4), 0xEF);            /* little-endian */
    CHECK_EQ(bank_peek(h, 5), 0xBE);

    bank_poke32(h, 8, 0x12345678UL);
    CHECK_EQ(bank_peek32(h, 8), 0x12345678UL);
    CHECK_EQ(bank_peek(h, 8), 0x78);

    /* bank_calloc must actually clear, including the tail of the class. */
    h = bank_calloc(5);
    for (i = 0; i < 8; ++i)
        CHECK_EQ(bank_peek(h, i), 0);

    bankmem_host_free();
}

static void test_free_and_reuse(void)
{
    handle_t a, b, c;
    bank_stats_t st;

    setup();

    a = bank_alloc(64);
    bank_poke(a, 0, 0x11);
    bank_free(a);

    /* The same class must come straight back off the free list. */
    b = bank_alloc(64);
    CHECK_EQ(H_BANK(a), H_BANK(b));
    CHECK_EQ(H_OFF(a), H_OFF(b));

    /* A different class must not. */
    bank_free(b);
    c = bank_alloc(256);
    CHECK(H_OFF(c) != H_OFF(a) || H_BANK(c) != H_BANK(a));

    bank_stats(&st);
    CHECK_EQ(st.recycled, 64);          /* the freed 64-byte block */

    /* Freeing the null handle is a documented no-op, not a crash. */
    bank_free(H_NULL);

    bankmem_host_free();
}

static void test_bank_rollover(void)
{
    handle_t h;
    uint16_t i;
    uint8_t first_bank_seen, saw_second = 0;

    setup();

    /* 8 KB of 1 KB blocks exactly fills bank 1; the ninth must move on. */
    h = bank_alloc(1024);
    first_bank_seen = H_BANK(h);
    for (i = 1; i < 16; ++i) {
        h = bank_alloc(1024);
        CHECK(h != H_NULL);
        if (H_BANK(h) != first_bank_seen)
            saw_second = 1;
    }
    CHECK(saw_second);

    bankmem_host_free();
}

static void test_no_block_straddles_a_bank(void)
{
    handle_t h;
    uint8_t i;

    setup();

    /* Leave 4 KB free in bank 1, then ask for 8 KB. It must not be split
     * across the boundary — the host backend aborts if it ever is. */
    for (i = 0; i < 4; ++i)
        CHECK(bank_alloc(1024) != H_NULL);

    h = bank_alloc(BANK_SIZE);
    CHECK(h != H_NULL);
    CHECK_EQ(H_OFF(h), 0);              /* must start at a bank boundary */
    bank_write(h, 0, "x", 1);
    bank_poke(h, BANK_SIZE - 1, 0x5A);  /* last byte of the block */
    CHECK_EQ(bank_peek(h, BANK_SIZE - 1), 0x5A);

    bankmem_host_free();
}

static void test_exhaustion(void)
{
    uint16_t n = 0;

    setup();

    /* Must fail cleanly rather than wrap or corrupt. */
    while (bank_alloc(BANK_SIZE) != H_NULL) {
        if (++n > TEST_BANKS + 4)
            break;
    }
    CHECK(n <= TEST_BANKS);
    CHECK_EQ(bank_alloc(8), H_NULL);

    bankmem_host_free();
}

static void test_realloc(void)
{
    handle_t h, g;
    uint8_t buf[64];
    uint8_t i;

    setup();

    h = bank_alloc(16);
    for (i = 0; i < 16; ++i)
        bank_poke(h, i, (uint8_t)(i + 1));

    /* Growing within the class keeps the same block. */
    g = bank_realloc(h, 16, 16);
    CHECK_EQ(g, h);

    /* Growing past it moves and copies. */
    g = bank_realloc(h, 16, 64);
    CHECK(g != H_NULL);
    CHECK_EQ(bank_block_size(g), 64);
    bank_read(g, 0, buf, 16);
    for (i = 0; i < 16; ++i)
        CHECK_EQ(buf[i], i + 1);

    bankmem_host_free();
}

void test_bank(void)
{
    test_handles();
    test_alloc_basics();
    test_read_write();
    test_free_and_reuse();
    test_bank_rollover();
    test_no_block_straddles_a_bank();
    test_exhaustion();
    test_realloc();
}
