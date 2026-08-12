/* ovl_probe.c — smallest possible tenant of each overlay area.
 *
 * These exist to prove the overlay machinery end to end: that the linker
 * places them at $8000, that the build wraps them with a load header, that
 * ovl_require() fetches the right file, and that a call into a freshly
 * loaded overlay returns the value that overlay (and not another) defines.
 *
 * They stay in the tree as a regression check once the real importers move
 * in alongside them.
 */
#include "ovl_probe.h"

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY1")
#endif
uint16_t ovl1_probe(void) { return OVL_PROBE_MAGIC + 1; }
#ifdef __CC65__
#  pragma code-name (pop)
#endif

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY2")
#endif
uint16_t ovl2_probe(void) { return OVL_PROBE_MAGIC + 2; }
#ifdef __CC65__
#  pragma code-name (pop)
#endif

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY3")
#endif
uint16_t ovl3_probe(void) { return OVL_PROBE_MAGIC + 3; }
#ifdef __CC65__
#  pragma code-name (pop)
#endif

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY4")
#endif
uint16_t ovl4_probe(void) { return OVL_PROBE_MAGIC + 4; }
#ifdef __CC65__
#  pragma code-name (pop)
#endif

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY5")
#endif
uint16_t ovl5_probe(void) { return OVL_PROBE_MAGIC + 5; }
#ifdef __CC65__
#  pragma code-name (pop)
#endif

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY6")
#endif
uint16_t ovl6_probe(void) { return OVL_PROBE_MAGIC + 6; }
#ifdef __CC65__
#  pragma code-name (pop)
#endif

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY7")
#endif
uint16_t ovl7_probe(void) { return OVL_PROBE_MAGIC + 7; }
#ifdef __CC65__
#  pragma code-name (pop)
#endif

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY8")
#endif
uint16_t ovl8_probe(void) { return OVL_PROBE_MAGIC + 8; }
#ifdef __CC65__
#  pragma code-name (pop)
#endif
