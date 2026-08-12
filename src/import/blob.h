/* blob.h — a byte stream living in banked RAM.
 *
 * This is what the import phases pass to each other. An overlay cannot call
 * another overlay, so zip, inflate and the XLSX parsers cannot be chained
 * through function calls; each writes a blob and the next one reads it.
 *
 *     OVL_ZIP      archive entry  -> blob (raw, still compressed)
 *     OVL_INFLATE  blob           -> blob (the XML)
 *     OVL_XLSX     blob           -> cells
 *
 * A stored entry skips the middle step, its blob already being the XML.
 *
 * Deliberately not a general allocation. A blob is whole banks, written
 * once from start to end and then read once from start to end, which is
 * exactly what the pipeline does and makes the position arithmetic a shift
 * rather than a division. Two exist at a time at most.
 *
 * Resident, because all three overlays and the driver need it and it is
 * under 400 bytes.
 */
#ifndef X16S_BLOB_H
#define X16S_BLOB_H

#include "../util/errors.h"

/* Largest entry we will stage, in banks and in bytes. A worksheet beyond
 * this is refused rather than truncated; see the note in x16sheet.h about
 * the honest limit. */
/* 16 banks, 128 KB: the ceiling on how large a workbook will import.
 *
 * NOT the banked heap, which is far larger -- this is what one staged part
 * must fit in, and the worksheet XML is the largest part. On a typical
 * six-column sheet it lands at roughly 3,000 cells.
 *
 * RAISING IT COSTS GOLDEN RAM: the bank table is one byte per bank in each
 * of the two blob descriptors inside xctx, and golden RAM is scarcer than
 * banked RAM. It would also only help a 2 MB machine, since a staged part
 * and its expansion are live at once with the cell store beside them.
 * Figures in docs/design/files.md. */
#define BLOB_MAX_BANKS 16
#define BLOB_MAX_BYTES ((uint32_t)BLOB_MAX_BANKS * 8192UL)

typedef struct blob_s {
    /* The banks, listed rather than assumed consecutive.
     *
     * Requiring a contiguous run looks tidier and costs 48 bytes less, but
     * it breaks the moment a blob is freed and another allocated: freed
     * banks go back on the free list and come off it in reverse, so the
     * second blob gets descending bank numbers and the allocation fails.
     * An import that works only until it stages its second part is worse
     * than one that carries a small table. */
    uint8_t  bank[BLOB_MAX_BANKS];
    uint8_t  banks;             /* 0 when nothing is held */
    uint32_t len;               /* bytes written          */
    uint32_t pos;               /* read cursor            */
} blob_t;

/* Reserve room for `bytes`, rounded up to whole banks. ERR_LIMIT when the
 * machine has not got that much free — an honest refusal, since the
 * alternative is a half-imported workbook. */
err_t blob_alloc(blob_t *b, uint32_t bytes);
void  blob_free(blob_t *b);

/* Sequential write, from the start. */
void blob_reset_write(blob_t *b);
uint8_t blob_write(blob_t *b, const void *data, uint16_t len);

/* Sequential read, from the start. Returns bytes copied, 0 at the end. */
void     blob_reset_read(blob_t *b);
uint16_t blob_read(blob_t *b, void *out, uint16_t len);

/* Matches inflate_feed_t and xml_feed_t, so a blob can be handed straight
 * to either as its source. */
uint16_t blob_feed(void *ctx, void *out, uint16_t len);

#endif /* X16S_BLOB_H */
