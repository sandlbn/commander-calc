#define BLOB_OWNER_INF
#include "blob_ovl.h"

/* inflate_blob.c — expand one blob into another.
 *
 * The only thing OVL_INFLATE does for the importer. Kept apart from
 * inflate.c so the decoder itself stays a general streaming decoder with no
 * knowledge of where its bytes come from — which is what let the host suite
 * test it against zlib through a plain buffer.
 */
#include "inflate.h"
#include "blob.h"

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY8")
#  pragma rodata-name (push, "OVL8RO")
#  pragma bss-name (push, "OVL8BSS")
#endif

/* Shared with the self test: an inflate_t is far too large for the stack,
 * and the two never run at the same time. */
extern inflate_t inflate_shared_state;

/* Small on purpose. This is a hand-off buffer between the decoder and the
 * blob, both of which are happy to be called more often, and it competes for
 * overlay space with the 32 KB window's bookkeeping. */
static uint8_t out_buf[32];

/* `expanded` is what the archive said the entry expands to. It is used to
 * size the destination rather than trusted as the answer: the loop stops
 * when the decoder stops, and a mismatch is reported. */
err_t inflate_blob(blob_t *src, blob_t *dst, uint32_t expanded)
{
    inflate_t *s = &inflate_shared_state;
    uint16_t n;
    err_t e;

    e = blob_alloc(dst, expanded ? expanded : BLOB_MAX_BYTES);
    if (e != ERR_OK)
        return e;

    blob_reset_read(src);
    blob_reset_write(dst);

    e = inflate_init(s, blob_feed, src);
    if (e != ERR_OK)
        return e;

    while ((n = inflate_read(s, out_buf, sizeof out_buf)) != 0) {
        if (!blob_write(dst, out_buf, n)) {
            inflate_free(s);
            return ERR_LIMIT;
        }
    }

    /* The 32 KB window goes back before anything else is staged: an import
     * expands one part after another, and holding four banks per part runs
     * the machine out long before a real workbook is finished. */
    inflate_free(s);

    /* The compressed source is consumed. Freeing it here rather than in the
     * caller is what lets the blob layer live entirely inside the overlays:
     * a driver sequencing the phases never has to touch one. */
    blob_free(src);

    if (s->err != ERR_OK)
        return s->err;
    /* Producing a different number of bytes than the archive promised means
     * the stream and the directory disagree, which is corruption however
     * cleanly the decoder finished. */
    if (expanded && dst->len != expanded)
        return ERR_BADFORMAT;

    return ERR_OK;
}

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
