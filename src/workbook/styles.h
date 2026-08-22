/* styles.h — the workbook's cell formats.
 *
 * Style 0 always exists and is General/default, so a cell record's style
 * byte is never invalid and a new cell needs no style work at all.
 */
#ifndef X16S_STYLES_H
#define X16S_STYLES_H

#include "../util/errors.h"

typedef enum {
    NF_GENERAL = 0,
    NF_INTEGER,
    NF_DECIMAL,
    NF_CURRENCY,
    NF_PERCENT,
    NF_DATE,
    NF_TIME,
    NF_TEXT
} number_format_t;

#define STY_BOLD        0x01
/* The four edges of a cell. They are drawn on VERA's second layer, behind
 * the text, so a border takes no character away from the cell it marks and
 * needs no row of its own -- see docs/design/ui.md.
 *
 * The order matters: shifted down by three these are exactly the tile index
 * the border charset is generated in, so the renderer hands the style
 * straight to screen_border() with no translation. Keep them in step with
 * SCREEN_B_* in ui/screen.h.
 *
 * Bits 3..6 of `flags`; 0x80 is still free. Spare bits rather than a sixth
 * field on purpose: STYLE_SIZE must stay 5 or every .X16S written before
 * today stops opening. */
#define STY_BORD_L      0x08
#define STY_BORD_R      0x10
#define STY_BORD_T      0x20
#define STY_BORD_B      0x40
#define STY_BORD_ALL    0x78
#define STY_ALIGN_MASK  0x06
#define STY_ALIGN_AUTO  0x00    /* text left, everything else right */
#define STY_ALIGN_LEFT  0x02
#define STY_ALIGN_RIGHT 0x04
#define STY_ALIGN_CENTRE 0x06

typedef struct {
    uint8_t number_format;
    uint8_t foreground;
    uint8_t background;
    uint8_t flags;
    uint8_t decimal_places;
} cell_style_t;

#define STYLE_SIZE 5

err_t   styles_init(void);
void    styles_get(uint8_t id, cell_style_t *out);

/* Add a style, or return the id of an identical one already present.
 * ERR_LIMIT when the table is full. */
err_t   styles_add(const cell_style_t *s, uint8_t *id);
uint8_t styles_count(void);

/* Append without de-duplicating. Only the file loader uses this: styles are
 * written in id order and cells refer to them by index, so collapsing two
 * identical entries on the way in would silently re-point cells. */
err_t styles_append(const cell_style_t *s);

#endif /* X16S_STYLES_H */
