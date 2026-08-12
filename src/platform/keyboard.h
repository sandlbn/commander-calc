/* keyboard.h — one key at a time, PETSCII, non-blocking.
 *
 * Returns 0 when nothing is waiting. Codes are the KERNAL's, tabulated in
 * "X16 Reference - 03 - Editor.md": $11/$91 cursor down/up, $1D/$9D right/
 * left, $02/$82 page down/up, $13/$93 home/clear, $09/$18 tab/shift-tab.
 */
#ifndef X16S_KEYBOARD_H
#define X16S_KEYBOARD_H

#include "../x16sheet.h"

/* Non-blocking: 0 when nothing is waiting. The main loop uses this. */
uint8_t kbd_get(void);

/* Blocks until a key arrives. Modal dialogs use this.
 *
 * On the host it drains the scripted queue and then returns ESC, so a test
 * that opens a dialog without scripting an answer cancels out instead of
 * hanging forever. */
uint8_t kbd_wait(void);

#ifdef X16S_HOST
/* Feed a scripted key sequence, so navigation can be driven from tests. */
void kbd_host_push(const uint8_t *keys, uint8_t n);

/* Empty the queue. Every test setup should call this: the queue does not
 * wrap, so keys left behind eventually fill it and later pushes are lost.
 * See the note beside the definition. */
void kbd_host_reset(void);
#endif

#endif /* X16S_KEYBOARD_H */
