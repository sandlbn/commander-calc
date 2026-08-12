/* cells.h — sparse cell storage.
 *
 * A worksheet with values in A1, B1 and A10000 holds three cell records, not
 * ten thousand rows of anything.
 *
 *   row_dir[256]                one handle per 256-row band, 1 KB, allocated once
 *     -> band[256]              one row_record inline per row, 2 KB per used band
 *          -> cells[]           cell_record_t sorted by column, grown by doubling
 *
 * Row records live *inside* the band array rather than behind their own
 * handle. That removes an allocation and an indirection from every row
 * lookup, which is the operation the renderer performs most.
 *
 * The cost is 2 KB per touched 256-row band: rows 1..100 cost 3 KB in
 * total, while one cell in each of 256 scattered bands costs 513 KB.
 * X16S_MAX_CELLS bounds that case.
 */
#ifndef X16S_CELLS_H
#define X16S_CELLS_H

#include "../util/errors.h"

typedef struct {
    handle_t row_dir;
    uint16_t cell_count;
    uint16_t max_row;        /* used range; only meaningful if cell_count */
    uint16_t max_col;
} cellstore_t;

err_t cells_init(cellstore_t *cs);
/* Forget every cell without walking the blocks -- only safe when the whole
 * heap is going. Defined in sheet_admin.c, in the overlay whose sheet-delete
 * command is its one caller, rather than resident. */
void  cells_reset(cellstore_t *cs);

/* Insert or overwrite. The caller owns any string reference in `rec`;
 * cells_set does not touch reference counts. */
err_t cells_set(cellstore_t *cs, uint16_t row, uint16_t col,
                const cell_record_t *rec);

/* 1 and fills `out` if the cell exists, 0 otherwise. */
uint8_t cells_get(const cellstore_t *cs, uint16_t row, uint16_t col,
                  cell_record_t *out);

/* 1 if a cell was removed. Reference counts are again the caller's. */
uint8_t cells_delete(cellstore_t *cs, uint16_t row, uint16_t col);

/* Cells present in a row, and access by position — for writing a worksheet
 * out in order without probing every column.
 *
 * NOT DECLARED HERE. Every caller is in an overlay, so they live in
 * cells_iter.c, which each of those overlays #includes; see cells_priv.h.
 * The renderer does not use them — it asks for one cell at a time. */

/* Lowest populated row >= `from`, or X16S_MAX_ROWS if there is none. */
uint16_t cells_next_row(const cellstore_t *cs, uint16_t from);

#endif /* X16S_CELLS_H */
