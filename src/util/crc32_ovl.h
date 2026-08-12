/* crc32_ovl.h — give an overlay its own copy of the checksum.
 *
 * Same arrangement as file_io_ovl.h and blob_ovl.h: an overlay cannot call
 * another overlay, and 241 bytes of code plus a 64-byte table is cheaper
 * duplicated than made resident.
 */
#ifndef X16S_CRC32_OVL_H
#define X16S_CRC32_OVL_H

#ifdef __CC65__
#  ifdef CRC32_OWNER_XSHEET
#    define crc32_init    hcrc32_init
#    define crc32_update  hcrc32_update
#    define crc32_final   hcrc32_final
#    define crc32_buffer  hcrc32_buffer
#  endif
#  ifdef CRC32_OWNER_XSAVE
#    define crc32_init    xcrc32_init
#    define crc32_update  xcrc32_update
#    define crc32_final   xcrc32_final
#    define crc32_buffer  xcrc32_buffer
#  endif
#  ifdef CRC32_OWNER_SAVE
#    define crc32_init    scrc32_init
#    define crc32_update  scrc32_update
#    define crc32_final   scrc32_final
#    define crc32_buffer  scrc32_buffer
#  endif
#endif

#include "crc32.h"

#endif /* X16S_CRC32_OVL_H */
