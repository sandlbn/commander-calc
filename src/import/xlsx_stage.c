#define BLOB_OWNER_ZIP
#include "blob_ovl.h"

#define FILEIO_OWNER_ZIP
#include "../platform/file_io_ovl.h"

#include "xlsx_stage.h"
#include "zip.h"
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY6")
#  pragma rodata-name (push, "OVL6RO")
#  pragma bss-name (push, "OVL6BSS")
#endif

/* Both are too large for cc65's stack, and only one part is staged at a
 * time. */
static zip_t Z;
static uint8_t buf[128];

err_t xlsx_stage(const char *archive, const char *path,
                 blob_t *out, stage_info_t *info)
{
    zip_entry_t e;
    err_t err;
    uint16_t n;

    memset(info, 0, sizeof *info);

    err = zip_open(&Z, archive);
    if (err != ERR_OK)
        return err;

    err = zip_find(&Z, path, &e);
    if (err != ERR_OK) {
        zip_close(&Z);
        return err;
    }

    info->compressed = (uint8_t)(e.method == ZIP_DEFLATE);
    info->raw_size   = e.uncomp_size;
    info->crc        = e.crc;

    /* Room for the bytes as they are on disk. A stored entry's blob is
     * already the finished part; a deflated one gets expanded next. */
    err = blob_alloc(out, e.comp_size);
    if (err != ERR_OK) {
        zip_close(&Z);
        return err;
    }

    err = zip_open_entry(&Z, &e);
    if (err != ERR_OK) {
        zip_close(&Z);
        return err;
    }

    blob_reset_write(out);
    while ((n = zip_read(&Z, buf, sizeof buf)) != 0) {
        if (!blob_write(out, buf, n)) {
            zip_close(&Z);
            return ERR_LIMIT;
        }
    }

    zip_close(&Z);
    return ERR_OK;
}

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
