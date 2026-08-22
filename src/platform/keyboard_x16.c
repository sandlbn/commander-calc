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

/* Ctrl+B and PAGE DOWN are the same byte.
 *
 * PETSCII gives Ctrl+letter the code letter-64, so Ctrl+B is $02 -- and the
 * X16's PAGE DOWN key sends $02 too (Editor chapter, "New Control
 * Characters"). Nothing in the byte says which was pressed, so moving the
 * paging command elsewhere would not help: the PgDn key would still arrive
 * as a request to embolden something.
 *
 * kbdbuf_get_modifiers is the way out, and the KERNAL documents it for
 * exactly this: "detecting combinations of a regular key and a modifier key
 * in cases where there is no dedicated PETSCII code". Bit 2 is Control.
 *
 * It reports what is held NOW rather than what was held when the key was
 * queued. In a loop that polls the keyboard every pass that is the same
 * thing; the one way to fool it is to release Ctrl within a frame of B,
 * which pages down instead. Asked of no other key, so nothing else pays
 * for it. */
#define KBD_MODIFIERS 0xFEC0
#define MOD_CTRL      0x04

/* menu.h's MENU_BOLD, which grid.c dispatches as K_BOLD. Not included from
 * here: this is the platform layer and that is the UI's header. */
#define K_BOLD_CODE   0x9C
#define K_PGDN_CODE   0x02

static uint8_t held;

static uint8_t modifiers(void)
{
    __asm__("jsr %w", KBD_MODIFIERS);
    __asm__("sta %v", held);
    return held;
}

/* PAGE DOWN, or Ctrl+B? Only $02 has to ask. */
static uint8_t disambiguate(uint8_t k)
{
    if (k == K_PGDN_CODE && (modifiers() & MOD_CTRL))
        return K_BOLD_CODE;
    return k;
}

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
    return disambiguate(stop_taken(cbm_k_getin()));
}

uint8_t kbd_wait(void)
{
    uint8_t k;

    do {
        k = cbm_k_getin();
    } while (k == 0);

    return disambiguate(stop_taken(k));
}
