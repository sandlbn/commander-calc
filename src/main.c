/* main.c — start-up, the main key loop and shutdown.
 *
 * Nothing here calls printf. The UI writes straight to VERA through
 * screen.h, which is faster than CHROUT and keeps cc65's stdio out of the
 * resident image entirely.
 */
#include "x16sheet.h"
#include "ui/grid.h"
#include "platform/keyboard.h"
#include "platform/mouse.h"
#include "platform/bankmem.h"
#include "platform/banked_ram.h"
#include "workbook/workbook.h"
#include <string.h>

static const char S_free[] = " bytes free";
/* "2097152 bytes free" is 18 plus NUL. Golden RAM: build_status() fills it
 * before the first render. */
X16S_GOLDEN_BEGIN
static char status[19];
X16S_GOLDEN_END

#ifdef __CC65__
/* Stack canary.
 *
 * The C stack grows down from __OVLSTART__ and BSS ends __STACKSIZE__ below
 * it, and nothing checks that boundary at run time -- an overrun writes
 * through the bottom of the stack into BSS, corrupting a module unrelated
 * to the call chain that caused it. One byte, checked once per keypress.
 * The deepest chain in the program is an .xlsx import. */
extern uint8_t *const stack_floor;      /* stackfloor_x16.s */
#define STACK_FLOOR (stack_floor[0])
#define CANARY 0x5A
/* Shown in the status bar when the canary has been overwritten. */
static const char S_stack[] = "STACK!";
#else
#define STACK_FLOOR canary_dummy
#define CANARY 0
static uint8_t canary_dummy;
static const char S_stack[] = "";
#endif

/* Fill `status` with "<n> bytes free". Formatted by hand: printf for one
 * line would cost more resident space than every other module together. */
static void build_status(void)
{
    uint32_t v = bank_bytes_free();
    char tmp[11];
    uint8_t n = 0, i = 0;

    if (v == 0)
        tmp[n++] = '0';
    while (v) {
        tmp[n++] = (char)('0' + (uint8_t)(v % 10));
        v /= 10;
    }
    while (n)
        status[i++] = tmp[--n];
    strcpy(status + i, S_free);
}

/* See the write in main(). */
#define EMU_CMDKEYS (*(volatile uint8_t *)0x9FB7)

int main(void)
{
    uint8_t key;

    /* Before anything else: golden RAM holds statics the startup code does
     * not clear, and reading one first would read whatever the previous
     * program left at $0400. See banked_ram.h. */
    golden_init();

    /* Stop the emulator claiming Ctrl+V, Ctrl+F and the rest as its own
     * command keys, so Paste and Find reach the program. Harmless on real
     * hardware: $9FB0-$9FBF is unused I/O there. Ctrl+M is intercepted
     * whatever this says. See docs/design/platform.md. */
    EMU_CMDKEYS = 1;

    if (screen_init() != ERR_OK)
        return 1;
    if (bank_heap_init(1, 0) != ERR_OK)
        return 1;
    if (wb_init() != ERR_OK)
        return 1;
    if (grid_init() != ERR_OK)
        return 1;

    STACK_FLOOR = CANARY;
    build_status();
    grid_set_status(status);
    grid_render_all();

    mouse_begin();

    for (;;) {
        key = kbd_get();
        if (key && grid_key(key) == GRID_QUIT)
            break;

        /* Polled every pass, not only when a key arrived: the wheel is
         * reported as movement since the previous poll, so a skipped poll
         * discards it. */
        mouse_poll();
        if (grid_mouse(mouse_px, mouse_py, mouse_btn, mouse_whl) == GRID_QUIT)
            break;
        if (STACK_FLOOR != CANARY) {
            grid_set_status(S_stack);
            STACK_FLOOR = CANARY;
        }
    }

    /* Hand the machine back as it was found, in the reverse order it was
     * taken: the pointer, then the keys, then the screen.
     *
     * The charset is the one that bites if it is forgotten. screen_init()
     * turns ISO mode ON, and that is what makes the KEYBOARD return ASCII
     * -- leave it on and BASIC's own keyboard is wrong afterwards, in a way
     * that does not look like this program's doing by the time anyone
     * notices it. */
    mouse_end();
    EMU_CMDKEYS = 0;                    /* the emulator's keys are its own
                                         * again; see the write above */
    screen_clear(COLOR(COL_WHITE, COL_BLUE));
    screen_reset_charset();
    return 0;
}
