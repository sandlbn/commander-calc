/* blob_inf_x16.c — the blob layer, compiled again into overlay 8.
 * See blob_ovl.h for why this duplication exists. Nothing new here: the
 * same source with its entry points renamed and placed in another area.
 */
#define BLOB_OWNER_INF
#include "blob_ovl.h"

#define BLOB_NO_SEGMENT

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY8")
#  pragma rodata-name (push, "OVL8RO")
#endif

#include "blob.c"

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
