/* zipw.h — writing a ZIP archive, one stored entry at a time.
 *
 * The other half of zip.h, and deliberately much smaller. An .xlsx we write
 * does not have to be small, only valid, so nothing here compresses:
 *
 *   - A DEFLATE *compressor* is a Huffman encoder, a match finder and a
 *     32 KB window, which is more code than the decoder we already carry
 *     and would run at 8 MHz on a machine the user is waiting at.
 *   - Method 0 (stored) is part of the format, not a fallback. Excel,
 *     LibreOffice and Google Sheets all open stored .xlsx files, and our
 *     own reader has handled them since the ZIP milestone.
 *
 * The cost is size on disk -- roughly five times a compressed .xlsx -- for
 * not shipping a compressor. If that ever stops being a fair trade, this is
 * where deflate would go and nothing above it would change.
 *
 * A ZIP local header carries the size and checksum of the data that follows
 * it, so BOTH MUST BE KNOWN BEFORE THE FIRST BYTE IS WRITTEN. zipw_begin()
 * is told them, and the caller is expected to have produced the part once
 * already to measure it. That costs generating each part twice and buys an
 * entirely conventional archive with no large allocation to fail. The two
 * alternatives are in docs/design/files.md.
 */
#ifndef X16S_ZIPW_H
#define X16S_ZIPW_H

#include "../platform/file_io_ovl.h"
#include "../platform/file_io.h"
#include "../x16sheet.h"

/* "xl/worksheets/sheet16.xml" is the longest, at 25. */
#define ZIPW_NAME_MAX 28

/* Room in banked RAM for the central directory: 46 bytes a record plus its
 * name, for five fixed parts and one per worksheet. */
#define ZIPW_CD_BYTES ((5 + X16S_MAX_SHEETS) * (46 + ZIPW_NAME_MAX))

/* Method 0. See the header for why nothing here compresses. */
#define ZIPW_STORED 0

/* No table of entries.
 *
 * A ZIP's central directory repeats, at the end, what each local header
 * already said -- and by the time an entry opens, every field of its
 * directory record is known. So the record is built there and appended to
 * a block of banked RAM, which is streamed out at the end.
 *
 * The alternative, an array of entries, was 21 of them in each of the two
 * export overlays and neither had the room. This keeps a handle and a
 * length instead, and drops the limit on how many parts an archive may
 * hold at the same time. */
typedef struct {
    fstream_t f;
    uint32_t  pos;              /* bytes written so far = next offset */
    uint32_t  left;             /* of the open entry, for the check above */
    handle_t  cd;               /* the central directory, banked */
    uint16_t  cd_len;
    uint16_t  count;
    err_t     err;              /* sticky: checked once at the end */
} zipw_t;

/* The functions are not declared here: zipw.c is #included by its one
 * caller and every one of them is static. See the note at the top of it. */

#endif /* X16S_ZIPW_H */
