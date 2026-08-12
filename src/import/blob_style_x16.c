/* blob_style_x16.c — reader-only blob layer for OVL_STYLE. See blob_ovl.h. */
#define BLOB_OWNER_STYLE
#include "blob_ovl.h"
#define BLOB_NO_SEGMENT
#define BLOB_READER_ONLY
#ifdef __CC65__
#  pragma code-name (push, "OVERLAY9")
#  pragma rodata-name (push, "OVL9RO")
#endif
#include "blob.c"
#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
