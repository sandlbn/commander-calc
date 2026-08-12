/* csv.h — CSV import and export.
 *
 * Lives in OVL_CSV, with its own copy of the file layer (see
 * platform/file_io_ovl.h).
 *
 * The dialect is the one Excel, LibreOffice and Google Sheets all write:
 * comma separated, fields quoted only when they need to be, a doubled quote
 * inside a quoted field, and either line ending. Reading is deliberately
 * more permissive than writing.
 */
#ifndef X16S_CSV_H
#define X16S_CSV_H

#include "../util/errors.h"

typedef struct {
    uint16_t rows;
    uint16_t cells;
    uint16_t skipped;       /* cells dropped at a limit                   */
    uint8_t  truncated;     /* 1 if the file was longer than we can hold  */
} csv_result_t;

/* Import at (row,col) as the top-left corner. Replaces whatever is there.
 * Type detection is the same one cell entry uses, so a CSV number becomes a
 * number and 007 stays text. */
err_t csv_import(const char *name, uint16_t row, uint16_t col,
                 csv_result_t *res);

/* Export the used range. `formulas` writes a formula's source rather than
 * its value — useful for moving a sheet between machines, useless for
 * anything that wants to read the numbers. */
err_t csv_export(const char *name, uint8_t delimiter, uint8_t formulas);

#endif /* X16S_CSV_H */
