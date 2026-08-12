/* crc32.h — the standard reflected CRC-32 (polynomial $EDB88320).
 *
 * Used for ZIP entry verification and for the .X16S trailer. Computed
 * incrementally so a stream never has to be buffered.
 *
 *     uint32_t c = crc32_init();
 *     c = crc32_update(c, chunk, len);      // repeat
 *     c = crc32_final(c);
 *
 * No 256-entry table: at 1 KB it would be a meaningful slice of the 26 KB
 * resident budget, and the nibble-wise variant used here needs only 16
 * entries for about half the speed. If profiling in step 15 says CRC is hot,
 * this is the obvious thing to trade back.
 */
#ifndef X16S_CRC32_H
#define X16S_CRC32_H

#include "../x16sheet.h"

uint32_t crc32_init(void);
uint32_t crc32_update(uint32_t crc, const void *data, uint16_t len);
uint32_t crc32_final(uint32_t crc);

/* One-shot convenience for whole buffers. */
uint32_t crc32_buffer(const void *data, uint16_t len);

#endif /* X16S_CRC32_H */
