/* inflate.h — streaming DEFLATE decoder (RFC 1951).
 *
 * Decodes without ever holding the compressed or the decompressed entry in
 * memory. The caller pulls bytes out; the decoder pulls its input from a
 * callback, which for XLSX is zip_read.
 *
 *     inflate_init(&s, feed, ctx);
 *     while ((n = inflate_read(&s, buf, sizeof buf)) > 0) { ... }
 *     if (s.err != ERR_OK) ...
 *
 * The 32 KB history window is the one part that cannot be small: a match can
 * reference anything in the previous 32768 bytes. It lives in banked RAM,
 * four banks, which is why this needs the allocator rather than a buffer.
 *
 * Worksheet XML compresses heavily, which is what makes an .xlsx readable
 * at all on a machine with 38 KB of main RAM.
 */
#ifndef X16S_INFLATE_H
#define X16S_INFLATE_H

#include "../util/errors.h"

/* Supplies the next chunk of compressed input. Returns 0 at the end.
 * zip_read has exactly this shape. */
typedef uint16_t (*inflate_feed_t)(void *ctx, void *buf, uint16_t len);

#define INF_IN_BUF   64         /* compressed input, main RAM  */
#define INF_WIN_BITS 15
#define INF_WIN_SIZE 32768UL    /* four banks of history       */

/* Huffman decoding tables. A canonical code is fully described by the count
 * of codes at each length plus the symbols in canonical order, which is far
 * smaller than a lookup table and decodes with one compare per bit — the
 * right trade when a full table would not fit anyway.
 *
 * Literals and distances get separate types because their alphabets are very
 * different sizes: 288 symbols against 30. One shared type would put 288
 * entries in both and waste 516 bytes, which is most of an overlay's spare
 * room. The code-length alphabet has 19 symbols and borrows the distance
 * table before the real one is built. */
typedef struct {
    uint16_t count[16];
    uint16_t symbol[288];
} hufflit_t;

typedef struct {
    uint16_t count[16];
    uint16_t symbol[30];
} huffdist_t;

typedef struct {
    inflate_feed_t feed;
    void          *ctx;

    uint8_t  in[INF_IN_BUF];
    uint16_t in_len, in_pos;

    /* LSB-first, as DEFLATE requires.
     *
     * Thirty-two bits, and measured rather than assumed: a 16-bit
     * accumulator needs reads wider than 8 bits composed from two calls,
     * and that composition cost 104 bytes more than cc65's 32-bit shift
     * helper, which is a compact runtime call rather than inline code. */
    uint32_t bitbuf;
    uint8_t  bitcnt;

    /* The 32 KB history window: four banks, listed rather than assumed
     * consecutive. Freed banks come back off the free list in reverse, so a
     * second stream would get descending bank numbers and a contiguous
     * window would fail to allocate. */
    uint8_t  win_bank[4];
    uint8_t  win_banks;         /* 0 when none are held           */
    uint16_t win_pos;           /* next write position, & 32767   */
    uint8_t  win_wrapped;

    uint8_t  last_block;
    uint8_t  state;
    uint16_t block_left;        /* stored: bytes remaining        */

    /* Only one pair of tables is live at a time: a fixed block builds them
     * from the RFC's fixed lengths, a dynamic one from its own header. */
    hufflit_t  lit;
    huffdist_t dist;
    uint8_t    have_tables;

    /* A match in progress, carried across inflate_read calls. */
    uint16_t copy_len, copy_dist;

    err_t    err;
    uint8_t  done;
} inflate_t;

/* An inflate_t is around 800 bytes — too large for cc65's stack, and larger
 * than all the resident memory left. Callers on the X16 must put it in their
 * overlay's own bss. */
err_t inflate_init(inflate_t *s, inflate_feed_t feed, void *ctx);

/* Give the window's banks back. An importer inflates one part after
 * another, and 32 KB leaked per part exhausts the machine well before a
 * real workbook is finished. */
void  inflate_free(inflate_t *s);

/* Decode up to `len` bytes. Returns the number produced; 0 means the stream
 * ended or failed — check s->err to tell which. */
uint16_t inflate_read(inflate_t *s, void *buf, uint16_t len);

/* Expand `src` into `dst`, sizing the destination from what the archive says
 * the entry expands to. Declared here rather than in blob.h because it is
 * the decompressor's half of the pipeline. */
struct blob_s;
err_t inflate_blob(struct blob_s *src, struct blob_s *dst, uint32_t expanded);

#endif /* X16S_INFLATE_H */
