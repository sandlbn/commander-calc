/* mouse.h — the pointer, as four variables and two calls.
 *
 * The KERNAL does the hard part. It owns the PS/2 packets, the sprite that
 * draws the pointer, and the clamping to the screen edges; mouse_scan() is
 * already being called by the default interrupt handler on every frame, so
 * polling here is just reading state that is being kept up to date anyway.
 * That is why this is a poll in the main loop rather than an interrupt
 * handler of our own -- there is nothing to handle.
 *
 * State in variables rather than through a struct pointer, because the X16
 * side is assembly: the KERNAL's mouse_get writes the position straight into
 * zero page, so mouse_px/mouse_py ARE that zero-page location and the poll
 * costs a jsr and two stores with nothing copied afterwards. A struct would
 * have meant marshalling a pointer through cc65's calling convention to buy
 * nothing.
 *
 * Position is in pixels with the origin at the top left, which is what the
 * KERNAL reports and what the caller has to turn into character cells --
 * see grid_mouse(), which is where the screen mode is accounted for.
 */
#ifndef X16S_MOUSE_H
#define X16S_MOUSE_H

#include "../util/errors.h"

/* Where the pointer is, in pixels.
 *
 * These two are in zero page on the X16 -- the KERNAL's mouse_get will only
 * write there -- and the compiler has to be told, or it addresses them
 * absolutely and ld65 warns about the mismatch. The generated code is
 * shorter for knowing, which is the usual reason to put anything in zero
 * page, but correctness is why the pragma is here. */
extern uint16_t mouse_px, mouse_py;
#ifdef __CC65__
#  pragma zpsym ("mouse_px")
#  pragma zpsym ("mouse_py")
#endif

/* Bit 0 left, bit 1 right, bit 2 middle. The KERNAL reports more; nothing
 * here looks above bit 1. */
#define MOUSE_LEFT   0x01
#define MOUSE_RIGHT  0x02
extern uint8_t mouse_btn;

/* Scroll wheel movement since the last poll, signed: positive is towards
 * the user, which is the direction that should scroll a document down. Zero
 * on a mouse without a wheel, and zero on every poll but the one that sees
 * the movement -- it is a delta, not a position, so a caller that skips a
 * poll loses that motion. */
extern int8_t mouse_whl;

/* Show the pointer and start tracking. Safe with no mouse plugged in: the
 * position simply never changes and no button is ever down. */
void mouse_begin(void);

/* Refresh all four variables above. Cheap enough to call every time round
 * the main loop, which is what the wheel being a delta requires. */
void mouse_poll(void);

#endif /* X16S_MOUSE_H */
