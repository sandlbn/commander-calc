/* xlsx_sheet.h — worksheet values, styles and dates.
 *
 * The last two parts of an .xlsx that carry data. Lives in its own overlay
 * with a second copy of the XML tokenizer: OVL_XLSX has ~85 bytes of area
 * left once the tokenizer, the workbook parsers and their buffers are in it.
 *
 * Both functions read an already-expanded part out of a blob, the same way
 * the workbook parsers do.
 */
#ifndef X16S_XLSX_SHEET_H
#define X16S_XLSX_SHEET_H

#include "blob.h"
#include "xlsx.h"

/* --- styles ---------------------------------------------------------- */

/* Excel names a cell's format by an index into <cellXfs>, and each of those
 * names a number format by id. The flattened result is, for every xf index,
 * the number format we will actually use and how many decimals it asks for.
 *
 * It lives in banked RAM rather than in a struct, because the driver that
 * carries it between phases is resident and 512 bytes there is most of the
 * budget. Two bytes per xf index: format, then places.
 *
 * Only the number format is taken. Fonts, fills and borders are read past —
 * a workbook must open whether or not we can reproduce how it looked. */
#define XLSX_STYLE_BYTES (X16S_MAX_STYLES * 2)
#define XLSX_STYLE_FMT(i)    ((uint16_t)((i) * 2))
#define XLSX_STYLE_PLACES(i) ((uint16_t)((i) * 2 + 1))

typedef struct {
    uint8_t count;
    uint8_t dropped;                /* more xfs than we hold */
} xlsx_styles_info_t;

err_t xlsx_parse_styles(blob_t *xml, handle_t out, xlsx_styles_info_t *info);

/* --- worksheet ------------------------------------------------------- */

typedef struct {
    uint16_t cells;         /* stored                                    */
    uint16_t formulas;      /* cells that carried one                    */
    uint16_t dates;         /* numbers a style made into dates           */
    /* No count of what was dropped: it was incremented in seven places and
     * read in none. What matters is that something went missing, and that
     * is what the import dialog says. */
    uint8_t  truncated;     /* 1 if anything was dropped                 */
} xlsx_sheet_result_t;

/* Import one worksheet into the workbook at its own coordinates.
 *
 * `ids` is the banked block xlsx_parse_strings filled, mapping a
 * shared-string index to a pool id. `st` may be NULL, in which case every cell is General and no
 * number becomes a date.
 *
 * A cell carrying a formula stores Excel's cached result. The formula text
 * itself is milestone 14's problem; until then a cached value is the honest
 * thing to show, because this program cannot yet know whether it agrees. */
err_t xlsx_parse_sheet(blob_t *xml, handle_t ids, uint16_t id_count,
                       handle_t styles, uint8_t style_count,
                       xlsx_sheet_result_t *res);

/* Serial-to-date conversion lives in util/date.h: a date is a number that a
 * format makes look like one, so the grid needs it on every repaint and it
 * is resident rather than here. */

#endif /* X16S_XLSX_SHEET_H */
