/* xlsx_out1_x16.c — the first half of the .xlsx writer, in OVL_XLSX_OUT1.
 *
 * Opens the archive, writes the four parts whose content never varies, and
 * later closes it. xlsx_export.c is #included rather than compiled on its
 * own: everything in it is static, so the second half can include the same
 * source into a different overlay without a single symbol colliding.
 */
#define XW_PART 1
#define FILEIO_OWNER_XSAVE
#define CRC32_OWNER_XSAVE
#include "../platform/file_io_ovl.h"
#include "../util/crc32_ovl.h"

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY12")
#  pragma rodata-name (push, "OVL12RO")
#  pragma bss-name (push, "OVL12BSS")
#endif

#include "xlsx_export.c"

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
