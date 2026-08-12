/* xlsx_out2_x16.c — the second half of the .xlsx writer, in OVL_XLSX_OUT2.
 *
 * The number formats and the cells: everything that depends on what the
 * workbook actually holds. See xlsx_out1_x16.c for why this is an #include
 * of the same source rather than a separate module.
 */
#define XW_PART 2
#define FILEIO_OWNER_XSHEET
#define CRC32_OWNER_XSHEET
#include "../platform/file_io_ovl.h"
#include "../util/crc32_ovl.h"

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY13")
#  pragma rodata-name (push, "OVL13RO")
#  pragma bss-name (push, "OVL13BSS")
#endif

#include "xlsx_export.c"

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
