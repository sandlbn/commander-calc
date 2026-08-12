#include "test.h"
#include "../src/platform/bankmem.h"
#include "../src/platform/banked_ram.h"
#include "../src/platform/file_io.h"
#include "../src/import/inflate.h"
#include "../src/import/zip.h"
#include "../src/util/crc32.h"

/* Fixtures are produced by python's zlib, and the expected output is the
 * original text. Anything this decoder gets wrong is wrong against a
 * reference implementation, not against our own idea of the format. */

/* --- feeding from a plain buffer ------------------------------------ */

typedef struct {
    const uint8_t *data;
    uint16_t len, pos;
    uint16_t chunk;             /* how much to hand over per call */
} buf_src_t;

static uint16_t buf_feed(void *ctx, void *out, uint16_t want)
{
    buf_src_t *b = (buf_src_t *)ctx;
    uint16_t n = (uint16_t)(b->len - b->pos);

    if (n == 0)
        return 0;
    if (b->chunk && n > b->chunk)
        n = b->chunk;
    if (n > want)
        n = want;
    memcpy(out, b->data + b->pos, n);
    b->pos = (uint16_t)(b->pos + n);
    return n;
}

/* 8 banks is not enough for the 32 KB window plus overhead; use 16. */
static void setup(void)
{
    CHECK(bankmem_host_init(16));
    CHECK_EQ(bank_heap_init(1, 0), ERR_OK);
}

/* Decompress `comp` and compare against `want`. `chunk` throttles the input
 * so the resume paths get exercised. */
static void expand(const char *file, int line,
                   const uint8_t *comp, uint16_t clen,
                   const char *want, uint16_t out_chunk, uint16_t in_chunk)
{
    inflate_t s;
    buf_src_t src;
    /* Big enough for the long-distance fixture, which expands to 20 KB.
     * Only the test buffers whole output — on the X16 the consumer streams. */
    static uint8_t got[24576];
    uint16_t total = 0, n;
    uint16_t wlen = (uint16_t)strlen(want);

    setup();
    src.data = comp; src.len = clen; src.pos = 0; src.chunk = in_chunk;

    ++tests_checked;
    if (inflate_init(&s, buf_feed, &src) != ERR_OK) {
        test_report_fail(file, line, "inflate_init", "out of banked memory");
        bankmem_host_free();
        return;
    }

    while (total < sizeof got) {
        n = inflate_read(&s, got + total, out_chunk);
        if (n == 0)
            break;
        total = (uint16_t)(total + n);
    }

    ++tests_checked;
    if (s.err != ERR_OK) {
        char d[80];
        snprintf(d, sizeof d, "err %s after %u bytes",
                 err_message(s.err), total);
        test_report_fail(file, line, "inflate_read", d);
    }

    ++tests_checked;
    if (total != wlen) {
        char d[80];
        snprintf(d, sizeof d, "got %u bytes, want %u", total, wlen);
        test_report_fail(file, line, "length", d);
    } else if (memcmp(got, want, wlen) != 0) {
        uint16_t i;
        char d[120];
        for (i = 0; i < wlen && got[i] == (uint8_t)want[i]; ++i)
            ;
        snprintf(d, sizeof d, "first difference at byte %u: got %02X want %02X",
                 i, got[i], (uint8_t)want[i]);
        test_report_fail(file, line, "content", d);
    }

    bankmem_host_free();
}

#define EXPAND(c, cl, w) expand(__FILE__, __LINE__, (c), (cl), (w), 256, 0)

/* --- fixtures, byte-for-byte from python's zlib --------------------- */
#include "inflate_fixtures.h"

static void test_stored(void)
{
    /* No compression at all: the decoder must still get the framing right,
     * including the complement check on the length. */
    EXPAND(fx_stored, fx_stored_len, fx_stored_text);
}

static void test_fixed(void)
{
    /* Fixed Huffman: short inputs where a dynamic table would cost more
     * than it saves. This is what zlib emits for a few bytes. */
    EXPAND(fx_fixed, fx_fixed_len, fx_fixed_text);
}

static void test_dynamic(void)
{
    /* Dynamic Huffman, the case that matters: a worksheet's XML, 4411 bytes
     * from 59. Exercises the code-length alphabet, all three run-length
     * escapes, and long matches. */
    EXPAND(fx_sheet, fx_sheet_len, fx_sheet_text);
}

static void test_repeats(void)
{
    /* Matches that overlap themselves — distance 1, length 100 — are the
     * classic place a decoder that copies with memmove goes wrong. Byte at a
     * time through the window is the only correct way. */
    EXPAND(fx_runs, fx_runs_len, fx_runs_text);
}

static void test_long_distance(void)
{
    /* A back-reference reaching most of the way across the 32 KB window,
     * which is the whole reason the window is in banked RAM. */
    EXPAND(fx_far, fx_far_len, fx_far_text);
}

/* Output chunking must not change the result: a match can span a call, and
 * the decoder has to resume mid-copy. */
static void test_resume_across_calls(void)
{
    expand(__FILE__, __LINE__, fx_sheet, fx_sheet_len, fx_sheet_text, 1, 0);
    expand(__FILE__, __LINE__, fx_sheet, fx_sheet_len, fx_sheet_text, 7, 0);
    expand(__FILE__, __LINE__, fx_sheet, fx_sheet_len, fx_sheet_text, 4096, 0);
}

/* Nor must input chunking: the bit reader has to cross buffer refills. */
static void test_input_chunking(void)
{
    expand(__FILE__, __LINE__, fx_sheet, fx_sheet_len, fx_sheet_text, 256, 1);
    expand(__FILE__, __LINE__, fx_sheet, fx_sheet_len, fx_sheet_text, 256, 3);
    expand(__FILE__, __LINE__, fx_sheet, fx_sheet_len, fx_sheet_text, 256, 17);
}

/* Truncated input must stop with an error, not spin or invent data. */
static void test_truncated(void)
{
    inflate_t s;
    buf_src_t src;
    uint8_t got[512];
    uint16_t total = 0, n;

    setup();
    src.data = fx_sheet; src.len = (uint16_t)(fx_sheet_len / 2);
    src.pos = 0; src.chunk = 0;

    CHECK_EQ(inflate_init(&s, buf_feed, &src), ERR_OK);
    while ((n = inflate_read(&s, got, sizeof got)) != 0)
        total = (uint16_t)(total + n);

    CHECK(s.err != ERR_OK);
    bankmem_host_free();
}

/* --- through the ZIP reader, which is how it will really be used ---- */

static uint16_t zip_feed(void *ctx, void *buf, uint16_t len)
{
    return zip_read((zip_t *)ctx, buf, len);
}

static void test_zip_entry(void)
{
    zip_t z;
    zip_entry_t e;
    inflate_t s;
    static uint8_t got[8192];
    uint16_t total = 0, n;
    uint32_t crc;

    setup();
    file_host_set_root("build/host/sd");

    CHECK_EQ(zip_open(&z, "TEST.ZIP"), ERR_OK);
    CHECK_EQ(zip_find(&z, "xl/worksheets/sheet1.xml", &e), ERR_OK);
    CHECK_EQ(e.method, ZIP_DEFLATE);
    CHECK_EQ(zip_open_entry(&z, &e), ERR_OK);

    CHECK_EQ(inflate_init(&s, zip_feed, &z), ERR_OK);
    while (total < sizeof got) {
        n = inflate_read(&s, got + total, 128);
        if (n == 0)
            break;
        total = (uint16_t)(total + n);
    }

    CHECK_EQ(s.err, ERR_OK);
    /* The size and CRC in the archive are the reference. Matching both means
     * the decoder agrees with the writer bit for bit. */
    CHECK_EQ(total, e.uncomp_size);
    crc = crc32_buffer(got, total);
    CHECK_EQ(crc, e.crc);

    zip_close(&z);
    bankmem_host_free();
}

/* A stored entry through the same path: no decoder involved, so this
 * isolates the plumbing from the decompression. */
static void test_zip_stored_entry(void)
{
    zip_t z;
    zip_entry_t e;
    uint8_t got[64];
    uint16_t n;

    setup();
    file_host_set_root("build/host/sd");

    CHECK_EQ(zip_open(&z, "TEST.ZIP"), ERR_OK);
    CHECK_EQ(zip_find(&z, "xl/sharedStrings.xml", &e), ERR_OK);
    CHECK_EQ(e.method, ZIP_STORED);
    CHECK_EQ(zip_open_entry(&z, &e), ERR_OK);
    n = zip_read(&z, got, sizeof got);
    got[n] = '\0';
    CHECK_STR((char *)got, "shared strings go here");

    zip_close(&z);
    bankmem_host_free();
}

void test_inflate(void)
{
    test_stored();
    test_fixed();
    test_dynamic();
    test_repeats();
    test_long_distance();
    test_resume_across_calls();
    test_input_chunking();
    test_truncated();
    test_zip_entry();
    test_zip_stored_entry();
}
