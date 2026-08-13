/* file_io_xsheet_x16.c — the file layer, compiled again into OVL_XLSX_OUT2.
 * See file_io_ovl.h. The same source with its entry points renamed and
 * placed in another overlay area.
 */
#define FILEIO_OWNER_XSHEET
#include "file_io_ovl.h"

#define FILEIO_NO_SEGMENT
#define FILEIO_STREAM_ONLY      /* opens, writes, closes; never renames */
#define FILEIO_NO_PATHS         /* Export prompts for a name; it is
                                 * never one the file list handed us */

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY13")
#  pragma rodata-name (push, "OVL13RO")
#endif

#include "file_io_x16.c"

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
