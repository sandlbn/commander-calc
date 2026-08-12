#include "overlay.h"
#include "file_io.h"
#include <cbm.h>
#include <errno.h>

/* Build "OVLn.BIN" for overlay `n`, in a buffer reused by every call.
 *
 * Uppercase because that is what is on the card: src/charmap.h disables
 * cc65's ASCII-to-PETSCII translation, and uppercase A-Z is the one range
 * where the two encodings agree. Lowercase would not survive the trip.
 *
 * Composed rather than tabulated, which is cheaper past about a dozen
 * overlays -- see docs/design/memory.md. Ids of 20 and above would produce
 * the wrong name. */
X16S_GOLDEN_BEGIN
static char ovl_file[10];
X16S_GOLDEN_END

static const char *ovl_name(uint8_t id)
{
    uint8_t i = 3;

    ovl_file[0] = 'O';
    ovl_file[1] = 'V';
    ovl_file[2] = 'L';
    if (id >= 10) {
        ovl_file[i++] = '1';
        id = (uint8_t)(id - 10);
    }
    ovl_file[i++] = (char)('0' + id);
    ovl_file[i++] = '.';
    ovl_file[i++] = 'B';
    ovl_file[i++] = 'I';
    ovl_file[i++] = 'N';
    ovl_file[i]   = '\0';
    return ovl_file;
}

/* Which overlay is at $8000, or 0 for none. Golden RAM, so golden_init()
 * is what guarantees the initial 0. */
X16S_GOLDEN_BEGIN
static uint8_t resident;   /* 0 = none */
X16S_GOLDEN_END

/* Why the last load failed, for the caller to report: the BASIC error code
 * from cbm_load, or $0E for a bad overlay id. Never 0 after a failure.
 *
 * Resident because whatever reports it cannot live in an overlay -- an
 * overlay is precisely what did not arrive. Codes are listed at the write
 * below; see also docs/design/platform.md. */
X16S_GOLDEN_BEGIN
uint8_t ovl_status;
X16S_GOLDEN_END

err_t ovl_require(uint8_t id)
{
    if (id == 0 || id > OVL_COUNT) {
        ovl_status = 0x0E;
        return ERR_NOTFOUND;
    }
    if (id == resident)
        return ERR_OK;

    /* A failed load leaves $8000.. holding a torn mix of the old and new
     * overlay, so drop the residency claim before trying. */
    resident = 0;

    if (cbm_load(ovl_name(id), file_device(), 0) == 0) {
        /* The BASIC error code, not cbm_k_readst(): READST reports
         * serial-line trouble and reads 0 for a load the KERNAL refused
         * outright. 1 too many files, 2 file open, 4 file not found,
         * 5 device not present, 9 illegal device number. */
        ovl_status = (uint8_t)__oserror;
        return ERR_IO;
    }

    resident = id;
    return ERR_OK;
}
