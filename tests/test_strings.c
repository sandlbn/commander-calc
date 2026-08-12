/* test_strings.c — the string pool's own invariants.
 *
 * There was no suite for the pool: it was exercised only through the
 * workbook, which meant a hole in it stayed hidden until a saved file came
 * back unreadable. The first test here is the one that was missing.
 */
#include "test.h"
#include "../src/platform/bankmem.h"
#include "../src/platform/banked_ram.h"
#include "../src/workbook/strings.h"
#include "../src/workbook/strings_priv.h"

static void setup(void)
{
    CHECK(bankmem_host_init(32));
    bank_heap_init(1, 0);
    CHECK_EQ(strpool_init(), ERR_OK);
}

/* A freed id is dead, and stays dead when another is freed after it.
 *
 * The free list threads through the id table: freeing an id writes the
 * next free id into the slot its handle was in. strp_rec_of() returned
 * that link as though it were a handle, so a freed id read back as alive
 * with a small integer for a record -- bank 0, which on the machine is the
 * KERNAL's. The saved workbook came back holding keyboard tables.
 *
 * TWO frees are needed to see it. The first writes free_head, which is 0
 * and still reads as H_NULL, so a single free looks perfectly correct.
 */
static void test_freed_ids_are_dead(void)
{
    uint16_t a, b, c;

    setup();
    CHECK_EQ(strpool_add("first", 5, &a), ERR_OK);
    CHECK_EQ(strpool_add("second", 6, &b), ERR_OK);
    CHECK_EQ(strpool_add("third", 5, &c), ERR_OK);
    CHECK(strpool_alive(a) && strpool_alive(b) && strpool_alive(c));

    strpool_unref(a);
    CHECK_EQ(strpool_alive(a), 0);

    strpool_unref(b);                   /* this is the one that broke */
    CHECK_EQ(strpool_alive(b), 0);
    CHECK_EQ(strpool_alive(a), 0);
    CHECK(strpool_alive(c));            /* and the survivor is untouched */
}

/* A dead id has no length either. This is what the .X16S writer asks, and
 * what it got was the KERNAL's memory read as a 40960-byte string. */
static void test_freed_ids_have_no_length(void)
{
    uint16_t a, b;

    setup();
    CHECK_EQ(strpool_add("alpha", 5, &a), ERR_OK);
    CHECK_EQ(strpool_add("bravo", 5, &b), ERR_OK);
    strpool_unref(a);
    strpool_unref(b);

    CHECK_EQ(strpool_len(a), 0);
    CHECK_EQ(strpool_len(b), 0);
}

/* And the free list still works: ids come back and are usable. */
static void test_ids_are_reused(void)
{
    uint16_t a, b, c, d;
    char buf[16];

    setup();
    CHECK_EQ(strpool_add("one", 3, &a), ERR_OK);
    CHECK_EQ(strpool_add("two", 3, &b), ERR_OK);
    strpool_unref(a);
    strpool_unref(b);

    CHECK_EQ(strpool_add("three", 5, &c), ERR_OK);
    CHECK_EQ(strpool_add("four", 4, &d), ERR_OK);
    CHECK(strpool_alive(c) && strpool_alive(d));
    CHECK(c == a || c == b);            /* reused, not grown */
    CHECK_EQ(strpool_len(c), 5);
    strpool_get(c, buf, sizeof buf);
    CHECK_STR(buf, "three");
}

/* Reference counting: a string with two holders survives one release. */
static void test_refcounts(void)
{
    uint16_t a;

    setup();
    CHECK_EQ(strpool_add("shared", 6, &a), ERR_OK);
    strpool_ref(a);
    strpool_unref(a);
    CHECK(strpool_alive(a));
    strpool_unref(a);
    CHECK_EQ(strpool_alive(a), 0);
}

/* The integrity check reports a healthy pool, and reports a sick one.
 *
 * The second half matters more than the first: a checker that cannot fail
 * is worth nothing, and this one exists precisely because the pool went
 * wrong once in a way nothing noticed. The corruption injected here is the
 * exact shape of that bug -- a slot holding a small integer, which reads
 * as a handle in bank 0. */
static void test_verify(void)
{
    uint16_t a, b, off;
    handle_t chunk;

    setup();
    CHECK_EQ(strpool_add("one", 3, &a), ERR_OK);
    CHECK_EQ(strpool_add("two", 3, &b), ERR_OK);
    CHECK_EQ(strpool_verify(), 0);

    strpool_unref(a);
    strpool_unref(b);
    CHECK_EQ(strpool_verify(), 0);      /* freed is not broken */

    /* Now break one on purpose. Not with a bank of 0 -- that is what a
     * free slot looks like and strp_rec_of() correctly reads it as one --
     * but with a bank beyond the heap, which is what a wild handle looks
     * like and is the shape a stale or overwritten one takes. */
    CHECK_EQ(strpool_add("three", 5, &a), ERR_OK);
    CHECK_EQ(strpool_verify(), 0);
    chunk = strp_chunk[(a - 1) >> 11];
    off = (uint16_t)(((a - 1) & 2047) * 4);
    bank_poke32(chunk, off, (handle_t)0x00C80100UL);   /* bank 200 */
    CHECK_EQ(strpool_verify(), 1);
}

void test_strings(void)
{
    test_verify();
    test_freed_ids_are_dead();
    test_freed_ids_have_no_length();
    test_ids_are_reused();
    test_refcounts();
}
