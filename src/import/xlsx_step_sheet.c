/* xlsx_step_sheet.c — the cells, and the end of the import.
 *
 * The last step, so it returns 0 whatever happens: there is no next overlay
 * and the driver stops on it.
 */
#include "xlsx_ctx.h"
#include "../platform/overlay.h"
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY10")
#  pragma rodata-name (push, "OVL10RO")
#  pragma bss-name (push, "OVL10BSS")
#endif

uint8_t xlsx_step_sheet(void)
{
    xlsx_sheet_result_t res;

    memset(&res, 0, sizeof res);
    xctx.err = xlsx_parse_sheet(&xctx.part, xctx.ids, xctx.rep.strings,
                                xctx.styles, xctx.sinfo.count, &res);

    /* Filled in even when the parse failed part-way: the report is what
     * tells the user how much of their workbook did get through. */
    /* Added, not assigned: the report covers the whole workbook and this
     * runs once per sheet. */
    xctx.rep.cells    += res.cells;
    xctx.rep.formulas += res.formulas;
    xctx.rep.dates    += res.dates;
    if (res.truncated)
        xctx.rep.truncated = 1;
    if (xctx.target.lossy)
        xctx.rep.lossy = 1;

    return 0;
}

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
