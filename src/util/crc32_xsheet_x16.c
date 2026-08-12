/* crc32_xsheet_x16.c — CRC-32, compiled again into OVL_XLSX_OUT2.
 * See crc32_ovl.h. Every entry a stored ZIP holds carries a checksum, so
 * both halves of the writer need this.
 */
#define CRC32_OWNER_XSHEET
#include "crc32_ovl.h"

#define CRC32_NO_SEGMENT

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY13")
#  pragma rodata-name (push, "OVL13RO")
#endif

#include "crc32.c"

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
