/* file_io_inf_x16.c — the file layer, compiled again into OVL_INFLATE.
 * See file_io_ovl.h for why this duplication exists and when it goes away.
 *
 * NOT BUILT, and now probably never will be. The import pipeline hands off
 * through banked RAM rather than through temp files, so the decompressor
 * reads a blob and writes a blob and touches no file at all. Kept because
 * the FILEIO_STREAM_ONLY trim it introduced is useful, and because if a
 * workbook ever turns out to be too large to stage in banked RAM, spilling
 * to disk is the fallback and this is where it starts.
 */
#define FILEIO_INF_ENABLED 0

#if FILEIO_INF_ENABLED

#define FILEIO_OWNER_INF
#include "file_io_ovl.h"

#define FILEIO_NO_SEGMENT
#define FILEIO_STREAM_ONLY      /* opens, reads, writes; never renames */

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY8")
#  pragma rodata-name (push, "OVL8RO")
#endif

#include "file_io_x16.c"

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif

#endif /* FILEIO_INF_ENABLED */
