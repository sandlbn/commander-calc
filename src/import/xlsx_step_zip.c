/* xlsx_step_zip.c — staging the next part out of the archive.
 *
 * Lives in OVL_ZIP beside the archive reader, so the part names below cost
 * nothing resident. That matters more than it looks: a table of pointers to
 * these strings would put the strings themselves in resident memory, which
 * cc65 does for string literals whatever rodata-name says. A packed list
 * walked by hand keeps them here.
 */
#include "xlsx_ctx.h"
#include "xlsx_stage.h"
#include "../platform/overlay.h"
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY6")
#  pragma rodata-name (push, "OVL6RO")
#  pragma bss-name (push, "OVL6BSS")
#endif

/* The four parts whose paths are fixed. The fifth is the worksheet, and its
 * path is not known until the relationships have been read. */
static const char part_paths[] =
    "xl/workbook.xml\0"
    "xl/_rels/workbook.xml.rels\0"
    "xl/sharedStrings.xml\0"
    "xl/styles.xml\0";

static const char *path_for(uint8_t n)
{
    const char *p = part_paths;

    while (n--) {
        while (*p)
            ++p;
        ++p;
    }
    return p;
}

/* A workbook with no text at all has no sharedStrings.xml, and one that
 * never formatted anything has no styles.xml. Neither is a failure, and an
 * importer that refused them would refuse a great many real files. */
static uint8_t optional(uint8_t n)
{
    return n == XP_STRINGS || n == XP_STYLES;
}

uint8_t xlsx_step_zip(void)
{
    stage_info_t info;
    const char *path;
    err_t e;

    if (xctx.part_no == XP_SHEET) {
        path = xctx.target.u.path;
        if (!path[0]) {                 /* no relationship named the sheet */
            xctx.err = ERR_NOTFOUND;
            return 0;
        }
    } else {
        path = path_for(xctx.part_no);
    }

    e = xlsx_stage(xctx.archive, path, &xctx.staged, &info);
    if (e != ERR_OK) {
        if (e == ERR_NOTFOUND && optional(xctx.part_no)) {
            ++xctx.part_no;             /* skip it and carry on */
            return OVL_ZIP;             /* already loaded, so this is free */
        }
        /* A file whose very first part is missing is not an .xlsx at all,
         * which is a more useful thing to say than "not found". */
        xctx.err = (xctx.part_no == XP_WORKBOOK && e == ERR_NOTFOUND)
                       ? ERR_BADFORMAT : e;
        return 0;
    }

    xctx.raw_size = info.raw_size;

    if (!info.compressed) {
        /* Stored: the staged blob is already the part. Moving the struct
         * rather than copying the banks hands the banks over with it. */
        xctx.part = xctx.staged;
        memset(&xctx.staged, 0, sizeof xctx.staged);
        return XLSX_PARSER(xctx.part_no);
    }
    return OVL_INFLATE;
}

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
