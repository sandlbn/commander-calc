/* styles_load.c — appending styles in file order.
 *
 * See styles_priv.h for why this is here rather than in styles.c, and why
 * it may touch the table directly.
 */
#include "styles_priv.h"
#include "../platform/banked_ram.h"

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY2")
#  pragma rodata-name (push, "OVL2RO")
#endif

err_t styles_append(const cell_style_t *s)
{
    if (sty_table == H_NULL)
        return ERR_NOMEM;
    if (sty_count >= X16S_MAX_STYLES)
        return ERR_LIMIT;
    bank_write(sty_table, (uint16_t)(sty_count * STYLE_SIZE), s, STYLE_SIZE);
    ++sty_count;
    return ERR_OK;
}

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
