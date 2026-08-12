/* crc32_save_x16.c — CRC-32, compiled again into OVL_X16S_SAVE.
 *
 * See crc32_ovl.h. Nothing here is new code: it is the same source with the
 * entry points renamed and placed in a different overlay area.
 */
#define CRC32_OWNER_SAVE
#include "crc32_ovl.h"

#define CRC32_NO_SEGMENT

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY11")
#  pragma rodata-name (push, "OVL11RO")
#endif

#include "crc32.c"

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
