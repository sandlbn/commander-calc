/* blob_sheet_x16.c — the blob layer, compiled again into OVL_SHEET.
 * Reader-only: the worksheet parser consumes a blob and never builds one.
 * See blob_ovl.h.
 */
#define BLOB_OWNER_SHEET
#include "blob_ovl.h"

#define BLOB_NO_SEGMENT
#define BLOB_READER_ONLY

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY10")
#  pragma rodata-name (push, "OVL10RO")
#endif

#include "blob.c"

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
