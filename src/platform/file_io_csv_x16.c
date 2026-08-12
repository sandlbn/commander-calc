/* file_io_csv_x16.c — the file layer, compiled again into OVL_CSV.
 *
 * See file_io_ovl.h for why this duplication exists and when it goes away.
 * Nothing here is new code: it is the same source with the entry points
 * renamed and placed in a different overlay area.
 */
#define FILEIO_OWNER_CSV
#include "file_io_ovl.h"

#define FILEIO_NO_SEGMENT

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY5")
#  pragma rodata-name (push, "OVL5RO")
#endif

#include "file_io_x16.c"

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
