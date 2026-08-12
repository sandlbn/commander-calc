/* xlsx_stage.h — fetching one part of an .xlsx into banked RAM.
 *
 * Lives in OVL_ZIP, next to the archive reader. Finds an entry by name and
 * copies its bytes into a blob, still compressed if the entry is deflated;
 * expanding it is the next overlay's job.
 */
#ifndef X16S_XLSX_STAGE_H
#define X16S_XLSX_STAGE_H

#include "blob.h"

typedef struct {
    uint8_t  compressed;    /* 1 if the blob still needs inflating */
    uint32_t raw_size;      /* what it expands to, from the archive */
    uint32_t crc;
} stage_info_t;

/* Open `archive`, find `path`, and stage it. The blob is allocated here and
 * belongs to the caller. ERR_NOTFOUND if the archive has no such part —
 * which for an optional part like sharedStrings.xml is not a failure. */
err_t xlsx_stage(const char *archive, const char *path,
                 blob_t *out, stage_info_t *info);

#endif /* X16S_XLSX_STAGE_H */
