#include "test.h"
#include "../src/util/crc32.h"

void test_crc32(void)
{
    uint32_t c;

    /* Values every CRC-32 implementation agrees on. */
    CHECK_EQ(crc32_buffer("", 0), 0x00000000UL);
    CHECK_EQ(crc32_buffer("a", 1), 0xE8B7BE43UL);
    CHECK_EQ(crc32_buffer("123456789", 9), 0xCBF43926UL);
    CHECK_EQ(crc32_buffer("The quick brown fox jumps over the lazy dog", 43),
             0x414FA339UL);

    /* Streaming in pieces must equal the one-shot result: this is the
     * property the ZIP reader depends on. */
    c = crc32_init();
    c = crc32_update(c, "1234", 4);
    c = crc32_update(c, "5678", 4);
    c = crc32_update(c, "9", 1);
    CHECK_EQ(crc32_final(c), 0xCBF43926UL);

    /* Zero-length updates must not disturb the running value. */
    c = crc32_init();
    c = crc32_update(c, "", 0);
    c = crc32_update(c, "123456789", 9);
    c = crc32_update(c, "", 0);
    CHECK_EQ(crc32_final(c), 0xCBF43926UL);
}
