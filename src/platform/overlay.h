/* overlay.h — load one of the swappable code overlays into $8000.
 *
 * Only one overlay is resident at a time. Overlays never call each other and
 * keep no state across a swap: results go to banked RAM or to disk. Calling
 * into an overlay that is not currently loaded jumps into whatever the last
 * overlay left behind, so every entry point must go through ovl_require().
 *
 * On the host build every overlay is linked in permanently and these calls
 * are no-ops that always succeed.
 */
#ifndef X16S_OVERLAY_H
#define X16S_OVERLAY_H

#include "../util/errors.h"

/* Load overlay `id` (OVL_CSV / OVL_ZIP / OVL_XLSX) unless it is already the
 * resident one. Returns ERR_OK, ERR_NOTFOUND or ERR_IO. */
err_t ovl_require(uint8_t id);

/* The KERNAL status byte from the last failed load: $80 device not
 * present, $40 end of file, $02 timeout, $10 checksum. Zero until one
 * fails. */
extern uint8_t ovl_status;

/* Which overlay is resident right now, or 0 for none. */
uint8_t ovl_current(void);

/* Forget the resident overlay, forcing the next ovl_require() to reload.
 * Used after anything that may have overwritten $8000..$9EFF. */
void ovl_invalidate(void);

/* Override the drive overlays are read from. Defaults to the device the
 * program was loaded from, or 8. */
void ovl_set_device(uint8_t d);

#endif /* X16S_OVERLAY_H */
