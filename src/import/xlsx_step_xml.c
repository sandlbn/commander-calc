/* xlsx_step_xml.c — the three parts the XML overlay understands.
 *
 * The workbook (which sheets exist), the relationships (where the chosen
 * one lives) and the shared strings (what most text cells point at). They
 * share an overlay because they share the tokenizer, which is the largest
 * thing in it.
 */
#include "xlsx_ctx.h"
#include "../platform/overlay.h"

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY7")
#  pragma rodata-name (push, "OVL7RO")
#  pragma bss-name (push, "OVL7BSS")
#endif

uint8_t xlsx_step_xml(void)
{
    err_t e;

    switch (xctx.part_no) {
    case XP_WORKBOOK:
        e = xlsx_find_sheet(&xctx.part, xctx.sheet_no, &xctx.target);
        /* Every sheet is imported now, so nothing is skipped unless the
         * workbook has more than X16S_MAX_SHEETS -- which the driver
         * notices, not this. */
        xctx.rep.sheets_found = xctx.target.sheet_count;
        if (e != ERR_OK) {
            xctx.err = e;
            return 0;
        }
        break;

    case XP_RELS:
        e = xlsx_find_path(&xctx.part, &xctx.target);
        if (e != ERR_OK) {
            xctx.err = e;
            return 0;
        }
        break;

    default:                            /* XP_STRINGS */
        e = xlsx_parse_strings(&xctx.part, xctx.ids, XLSX_ID_MAX,
                               &xctx.rep.strings, &xctx.rep.lossy);
        if (e == ERR_LIMIT)
            xctx.rep.truncated = 1;         /* more strings than we map */
        else if (e != ERR_OK) {
            xctx.err = e;
            return 0;
        }
        break;
    }

    ++xctx.part_no;
    return OVL_ZIP;
}

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
