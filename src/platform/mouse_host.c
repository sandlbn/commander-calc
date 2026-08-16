/* mouse_host.c — no pointer off the machine.
 *
 * The variables exist so grid_mouse() compiles and can be driven directly by
 * the host tests, which is where the hit-testing arithmetic is actually
 * checked. Nothing here ever changes them: a test sets them, or passes its
 * own values to grid_mouse(), and there is no device to poll.
 */
#include "mouse.h"

uint16_t mouse_px, mouse_py;
uint8_t  mouse_btn;
int8_t   mouse_whl;

void mouse_begin(void) { }
void mouse_poll(void)  { }

void mouse_end(void) { }
