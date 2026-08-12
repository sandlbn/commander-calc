#include "keyboard.h"

static uint8_t queue[64];
static uint8_t head, tail;

/* Empty the queue and start again at the front.
 *
 * Every test must call this in its setup. head and tail only move forward,
 * so draining with kbd_get() is not equivalent: tail eventually reaches the
 * end of the array and kbd_host_push() silently drops keys, after which
 * kbd_wait() answers ESC and every dialog cancels itself. */
void kbd_host_reset(void)
{
    head = tail = 0;
}

void kbd_host_push(const uint8_t *keys, uint8_t n)
{
    while (n-- && tail < sizeof queue)
        queue[tail++] = *keys++;
}

uint8_t kbd_get(void)
{
    return head < tail ? queue[head++] : 0;
}

uint8_t kbd_wait(void)
{
    /* 0x1B is ESC: an unscripted dialog cancels rather than blocking. */
    return head < tail ? queue[head++] : 0x1B;
}
