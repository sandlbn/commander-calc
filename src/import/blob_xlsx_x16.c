/* blob_xlsx_x16.c — the blob layer, compiled again into overlay 7.
 * See blob_ovl.h for why this duplication exists. Nothing new here: the
 * same source with its entry points renamed and placed in another area.
 */
#define BLOB_OWNER_XLSX
#include "blob_ovl.h"

#define BLOB_NO_SEGMENT
#define BLOB_READER_ONLY   /* the parsers consume blobs, never build them */

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY7")
#  pragma rodata-name (push, "OVL7RO")
#endif

#include "blob.c"

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
