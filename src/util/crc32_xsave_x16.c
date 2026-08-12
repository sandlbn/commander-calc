/* crc32_xsave_x16.c — CRC-32, compiled again into OVL_XLSX_SAVE.
 * See crc32_ovl.h. Every entry a stored ZIP holds still carries a checksum,
 * so the writer needs this even though it never compresses.
 */
#define CRC32_OWNER_XSAVE
#include "crc32_ovl.h"

#define CRC32_NO_SEGMENT

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY12")
#  pragma rodata-name (push, "OVL12RO")
#endif

#include "crc32.c"

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
