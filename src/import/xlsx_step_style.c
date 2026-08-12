/* xlsx_step_style.c — the number formats.
 *
 * Without styles every number is General and no number is a date — visibly
 * plain rather than wrong, so a styles part that will not parse is not worth
 * failing the import over.
 */
#include "xlsx_ctx.h"
#include "../platform/overlay.h"

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY9")
#  pragma rodata-name (push, "OVL9RO")
#  pragma bss-name (push, "OVL9BSS")
#endif

uint8_t xlsx_step_style(void)
{
    if (xlsx_parse_styles(&xctx.part, xctx.styles, &xctx.sinfo) == ERR_OK) {
        xctx.rep.formats = xctx.sinfo.count;
        if (xctx.sinfo.dropped)
            xctx.rep.truncated = 1;
    }
    ++xctx.part_no;
    return OVL_ZIP;
}

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
