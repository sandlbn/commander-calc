/* cells_priv.h — the sparse index's layout, for the read-only walkers.
 *
 * cells_row_count() and cells_row_at() are how a whole sheet is walked in
 * order, and every caller is in an overlay: the .X16S writer, the .xlsx
 * writer, and the evaluator's recalculation pass. Nothing resident uses
 * them — the renderer asks for one cell at a time through cells_get().
 *
 * So they live in cells_iter.c, compiled once into each of those overlays,
 * and the 258 bytes come out of resident memory. They read the index and
 * never change it, which is what makes duplicating them safe: there is no
 * state here to get out of step, only a layout both halves must agree on.
 */
#ifndef X16S_CELLS_PRIV_H
#define X16S_CELLS_PRIV_H

#include "cells.h"
#include "../platform/banked_ram.h"

#define BAND_SHIFT   8
#define BAND_ROWS    (1 << BAND_SHIFT)          /* 256 rows per band */

/* row_record, inline in the band array */
#define RR_SIZE      8
#define RR_COUNT     0      /* uint16: cells in use   */
#define RR_CAP       2      /* uint16: capacity        */
#define RR_CELLS     4      /* handle: the cell array  */

#define CELL_SIZE    8      /* sizeof(cell_record_t), fixed by the format */

#define ROW_BAND(r)  ((uint16_t)((r) >> BAND_SHIFT))
#define RR_OFF(r)    ((uint16_t)(((r) & (BAND_ROWS - 1)) * RR_SIZE))

#endif /* X16S_CELLS_PRIV_H */
