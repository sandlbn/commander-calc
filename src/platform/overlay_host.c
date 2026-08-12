#include "overlay.h"

/* The host build links every overlay in permanently. */

static uint8_t resident;

uint8_t ovl_current(void) { return resident; }

void ovl_invalidate(void) { resident = 0; }

void ovl_set_device(uint8_t d) { (void)d; }

err_t ovl_require(uint8_t id)
{
    if (id == 0 || id > OVL_COUNT)
        return ERR_NOTFOUND;
    resident = id;
    return ERR_OK;
}

/* No overlays here, so no load can fail -- but grid.c reports this and the
 * host builds the same grid.c. Always zero. */
uint8_t ovl_status;
