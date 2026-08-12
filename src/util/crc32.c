#include "crc32.h"

#if defined(__CC65__) && !defined(CRC32_NO_SEGMENT)
/* The .X16S reader is here; the writer carries its own copy, because the
 * two are in different overlays and one cannot call the other. */
#  pragma code-name (push, "OVERLAY2")
#  pragma rodata-name (push, "OVL2RO")
#endif

/* Nibble table for the reflected polynomial $EDB88320. */
static const uint32_t crc_nib[16] = {
    0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
    0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
    0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
    0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL
};

uint32_t crc32_init(void)
{
    return 0xFFFFFFFFUL;
}

uint32_t crc32_update(uint32_t crc, const void *data, uint16_t len)
{
    const uint8_t *p = (const uint8_t *)data;

    while (len--) {
        crc ^= *p++;
        crc = (crc >> 4) ^ crc_nib[crc & 0x0F];
        crc = (crc >> 4) ^ crc_nib[crc & 0x0F];
    }
    return crc;
}

uint32_t crc32_final(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFUL;
}

uint32_t crc32_buffer(const void *data, uint16_t len)
{
    return crc32_final(crc32_update(crc32_init(), data, len));
}

#if defined(__CC65__) && !defined(CRC32_NO_SEGMENT)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
