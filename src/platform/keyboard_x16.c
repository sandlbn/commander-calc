#include "keyboard.h"
#include <cbm.h>

/* Reading a key also clears the KERNAL's stop flag.
 *
 * PETSCII $03 is Ctrl+C and RUN/STOP alike, and the KERNAL sets its stop
 * flag from the character as it enters the queue. Nothing clears it again
 * until the next key is queued, and LOAD abandons itself if it sees the
 * flag set -- so a Ctrl+C command would abort the overlay load that the
 * command itself needs. Once the key has been read it has been dealt with,
 * and the flag must not still be pending for the KERNAL.
 *
 * kbdbuf_put stores the flag unconditionally, even when the queue is full
 * and the character is dropped, so pushing any character other than $03
 * clears it. It is a documented KERNAL API; the flag's own address is not,
 * and moves with the ROM version. See docs/design/platform.md.
 *
 * Only on $03, never on the polling path: this appends to the queue, and
 * doing it on every kbd_get() would fill the buffer with zeros and the
 * KERNAL would begin dropping real keys. The character pushed is 0, which
 * every reader here already treats as "no key".
 */
#define KBDBUF_PUT 0xFEC3

static uint8_t stop_taken(uint8_t k)
{
    if (k == 3) {
        __asm__("lda #0");
        __asm__("jsr %w", KBDBUF_PUT);
    }
    return k;
}

uint8_t kbd_get(void)
{
    return stop_taken(cbm_k_getin());
}

uint8_t kbd_wait(void)
{
    uint8_t k;

    do {
        k = cbm_k_getin();
    } while (k == 0);

    return stop_taken(k);
}
