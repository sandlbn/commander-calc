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
