/* file_io_zip_x16.c — the file layer, compiled again into OVL_ZIP.
 * See file_io_ovl.h for why this duplication exists and when it goes away.
 */
#define FILEIO_OWNER_ZIP
#include "file_io_ovl.h"

#define FILEIO_NO_SEGMENT
#define FILEIO_WANT_SEEK        /* the only copy that seeks */

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY6")
#  pragma rodata-name (push, "OVL6RO")
#endif

#include "file_io_x16.c"

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
