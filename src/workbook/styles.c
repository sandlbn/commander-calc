#include "styles.h"
#include "styles_priv.h"
#include "../platform/banked_ram.h"
#include <string.h>

/* Zero page: every rendered cell looks its style up through these. */
X16S_ZP_BEGIN
handle_t sty_table;
uint8_t  sty_count;
X16S_ZP_END

/* Set the moment any style in the table carries a border, and never
 * cleared while the workbook is open. The renderer reads it once per
 * repaint to decide whether to bring the border layer up at all, so a
 * workbook with no borders in it pays nothing for the feature. Clearing
 * every border again leaves it set, which costs a layer that draws
 * nothing -- not worth a scan of the table to find out. */
uint8_t sty_bord;

err_t styles_init(void)
{
    cell_style_t def;

    sty_table = bank_calloc(X16S_MAX_STYLES * STYLE_SIZE);
    if (sty_table == H_NULL)
        return ERR_NOMEM;
    sty_count = 0;
    sty_bord = 0;

    /* Style 0 is General and is never removed, so every cell_record_t's
     * style byte resolves even before anything has been formatted. */
    memset(&def, 0, sizeof def);
    def.number_format = NF_GENERAL;
    def.decimal_places = 2;
    {
        uint8_t id;
        return styles_add(&def, &id);
    }
}

void styles_get(uint8_t id, cell_style_t *out)
{
    if (sty_table == H_NULL || id >= sty_count) {
        memset(out, 0, sizeof *out);
        out->decimal_places = 2;
        return;
    }
    bank_read(sty_table, (uint16_t)(id * STYLE_SIZE), out, STYLE_SIZE);
}

err_t styles_add(const cell_style_t *s, uint8_t *id)
{
    uint8_t i;
    cell_style_t got;

    if (sty_table == H_NULL)
        return ERR_NOMEM;

    /* Styles are shared aggressively: a workbook where a whole column is
     * formatted as currency should spend one sty_table slot, not one per cell.
     * With at most 128 entries a linear scan is cheaper than an index. */
    for (i = 0; i < sty_count; ++i) {
        bank_read(sty_table, (uint16_t)(i * STYLE_SIZE), &got, STYLE_SIZE);
        if (memcmp(&got, s, STYLE_SIZE) == 0) {
            *id = i;
            return ERR_OK;
        }
    }

    if (sty_count >= X16S_MAX_STYLES)
        return ERR_LIMIT;

    bank_write(sty_table, (uint16_t)(sty_count * STYLE_SIZE), s, STYLE_SIZE);
    if (s->flags & STY_BORD_ALL)
        sty_bord = 1;
    *id = sty_count++;
    return ERR_OK;
}


uint8_t styles_count(void) { return sty_count; }
