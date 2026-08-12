/* xml_style_x16.c — the XML tokenizer for OVL_STYLE. See xml_ovl.h. */
#define XML_OWNER_STYLE
#include "xml_ovl.h"
#define XML_NO_SEGMENT
#ifdef __CC65__
#  pragma code-name (push, "OVERLAY9")
#  pragma rodata-name (push, "OVL9RO")
#endif
#include "xml.c"
#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
