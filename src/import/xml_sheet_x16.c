/* xml_sheet_x16.c — the XML tokenizer, compiled again into OVL_SHEET.
 *
 * The tokenizer is 3743 bytes and both XLSX overlays need one. Duplicating
 * it is the same trade as everywhere else here: overlay space is available
 * and resident space is not, and an overlay cannot call another overlay.
 *
 * Renamed through XML_OWNER_SHEET so the two copies do not collide.
 */
#define XML_OWNER_SHEET
#include "xml_ovl.h"

#define XML_NO_SEGMENT

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY10")
#  pragma rodata-name (push, "OVL10RO")
#endif

#include "xml.c"

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
