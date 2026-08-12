#define FILEIO_OWNER_CSV
#include "../platform/file_io_ovl.h"

#include "csv.h"
#include "../platform/banked_ram.h"
#include "../workbook/strings.h"
#include "../formula/formula.h"
#include "../platform/file_io.h"
#include "../workbook/workbook.h"
#include "../workbook/cells.h"
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY5")
#  pragma rodata-name (push, "OVL5RO")
#endif

#define IO_BUF   64
#define FIELD_MAX 96        /* a cell's worth; longer fields are clipped */

/* --- a byte at a time, buffered ------------------------------------- */

typedef struct {
    fstream_t f;
    uint8_t   buf[IO_BUF];
    uint8_t   n, pos;
    uint8_t   done;
} rd_t;

static int16_t r_next(rd_t *r)
{
    if (r->pos == r->n) {
        if (r->done)
            return -1;
        r->n = (uint8_t)file_read(&r->f, r->buf, IO_BUF);
        r->pos = 0;
        if (r->n == 0) {
            r->done = 1;
            return -1;
        }
        if (file_eof(&r->f))
            r->done = 1;    /* this is the last block, but use it first */
    }
    return r->buf[r->pos++];
}

/* --- import ---------------------------------------------------------- */

/* Read one field. Returns the delimiter that ended it: ',' , '\n', or -1 at
 * end of file. `len` is what was actually kept.
 *
 * Quoting follows RFC 4180 as the three spreadsheets implement it: a field
 * that starts with a quote runs until the closing quote, and "" inside it is
 * a literal quote. Anything after the closing quote and before the delimiter
 * is discarded rather than treated as an error — real files contain it. */
static int16_t read_field(rd_t *r, char *out, uint8_t *len)
{
    int16_t c;
    uint8_t n = 0;
    uint8_t quoted = 0;

    c = r_next(r);
    if (c < 0) {
        *len = 0;
        return -1;
    }

    if (c == '"') {
        quoted = 1;
        c = r_next(r);
    }

    for (;;) {
        if (c < 0)
            break;

        if (quoted) {
            if (c == '"') {
                c = r_next(r);
                if (c != '"')
                    break;          /* closing quote */
                /* "" is one quote, and we are still inside the field. */
            }
        } else {
            if (c == ',' || c == '\n' || c == '\r')
                break;
        }

        if (n < FIELD_MAX - 1)
            out[n++] = (char)c;
        c = r_next(r);
    }

    /* After a closing quote, skip to the delimiter. */
    if (quoted)
        while (c >= 0 && c != ',' && c != '\n' && c != '\r')
            c = r_next(r);

    /* CRLF counts as one line ending, not two. */
    if (c == '\r') {
        c = r_next(r);
        if (c != '\n' && c >= 0)
            r->pos = (uint8_t)(r->pos ? r->pos - 1 : 0);
        c = '\n';
    }

    out[n] = '\0';
    *len = n;
    return c;
}

/* Put a formula by rather than compiling it here -- see the note at the
 * call. The list is the one the other two readers fill; the caller
 * allocates it and formula_compile_pending() works through it. */
static err_t note_formula(uint16_t row, uint16_t col, const char *src,
                          uint16_t len)
{
    uint8_t  ent[FPEND_FIELD];
    uint16_t sid;

    if (formula_pending == H_NULL || formula_pending_n >= FPEND_MAX)
        return ERR_LIMIT;
    if (strpool_add(src, len, &sid) != ERR_OK)
        return ERR_NOMEM;

    ent[0] = wb_sheet_i;
    ent[1] = (uint8_t)row;
    ent[2] = (uint8_t)(row >> 8);
    ent[3] = (uint8_t)col;
    ent[4] = (uint8_t)sid;
    ent[5] = (uint8_t)(sid >> 8);
    bank_write(formula_pending, (uint16_t)(formula_pending_n * FPEND_FIELD),
               ent, FPEND_FIELD);
    ++formula_pending_n;
    return ERR_OK;
}

err_t csv_import(const char *name, uint16_t row, uint16_t col,
                 csv_result_t *res)
{
    rd_t r;
    char field[FIELD_MAX];
    uint16_t cur_row = row, cur_col = col;
    uint8_t  len;
    int16_t  end;
    err_t e;

    /* Somewhere to put formulas, allocated here rather than by the caller
     * so that the resident side of this stays one call and not three. */
    formula_pending_n = 0;
    formula_pending = bank_alloc(FPEND_MAX * FPEND_FIELD);

    memset(res, 0, sizeof *res);
    memset(&r, 0, sizeof r);

    e = file_open_read(&r.f, name);
    if (e != ERR_OK)
        return e;

    for (;;) {
        end = read_field(&r, field, &len);
        if (end < 0 && len == 0)
            break;

        if (cur_row >= X16S_MAX_ROWS || cur_col >= X16S_MAX_COLS) {
            ++res->skipped;
            res->truncated = 1;
        } else if (len) {
            /* Same interpretation as typing the text into the cell, so a
             * CSV number becomes a number and 007 stays text.
             *
             * EXCEPT a formula. wb_set_text() compiles one, and compiling
             * means loading OVL_FCOMPILE -- which lands on top of THIS
             * overlay, so the call would return into code that no longer
             * exists. So a formula is put by for the resident caller to
             * compile once the importer has finished, exactly as the .xlsx
             * and .X16S readers do it. */
            if (field[0] == '=')
                e = note_formula(cur_row, cur_col, field, len);
            else
                e = wb_set_text(cur_row, cur_col, field);
            if (e == ERR_LIMIT) {
                res->truncated = 1;
                ++res->skipped;
            } else if (e != ERR_OK) {
                file_close(&r.f);
                return e;
            } else {
                ++res->cells;
            }
        }

        if (end == '\n' || end < 0) {
            ++res->rows;
            cur_row = (uint16_t)(cur_row + 1);
            cur_col = col;
            if (end < 0)
                break;
        } else {
            cur_col = (uint16_t)(cur_col + 1);
        }
    }

    file_close(&r.f);
    return ERR_OK;
}

/* --- export ----------------------------------------------------------- */

typedef struct {
    fstream_t f;
    uint8_t   buf[IO_BUF];
    uint8_t   n;
    err_t     err;
} wr_t;

static void w_flush(wr_t *w)
{
    if (w->err != ERR_OK || w->n == 0)
        return;
    if (file_write(&w->f, w->buf, w->n) != w->n)
        w->err = ERR_IO;
    w->n = 0;
}

static void w_byte(wr_t *w, char c)
{
    if (w->err != ERR_OK)
        return;
    w->buf[w->n++] = (uint8_t)c;
    if (w->n == IO_BUF)
        w_flush(w);
}

/* Quote only when the text would otherwise be ambiguous, which is what the
 * three spreadsheets do and what keeps a file of plain numbers readable. */
static void w_field(wr_t *w, const char *s, uint8_t delim)
{
    const char *p;
    uint8_t need = 0;

    for (p = s; *p; ++p)
        if (*p == (char)delim || *p == '"' || *p == '\n' || *p == '\r') {
            need = 1;
            break;
        }

    if (!need) {
        while (*s)
            w_byte(w, *s++);
        return;
    }

    w_byte(w, '"');
    while (*s) {
        if (*s == '"')
            w_byte(w, '"');     /* doubled */
        w_byte(w, *s++);
    }
    w_byte(w, '"');
}

err_t csv_export(const char *name, uint8_t delimiter, uint8_t formulas)
{
    wr_t w;
    const cellstore_t *cs = wb_cells();
    uint16_t row;
    err_t e;

    memset(&w, 0, sizeof w);
    if (delimiter == 0)
        delimiter = ',';

    e = file_open_write(&w.f, name);
    if (e != ERR_OK)
        return e;

    if (cs->cell_count) {
        for (row = 0; row <= cs->max_row; ++row) {
            uint16_t c;
            for (c = 0; c <= cs->max_col; ++c) {
                char text[WB_TEXT_MAX];

                if (c)
                    w_byte(&w, (char)delimiter);
                /* Rows inside the used range that hold nothing still emit
                 * their separators, so columns stay aligned in the output. */
                if (formulas)
                    wb_edit_text(row, c, text, sizeof text);
                else
                    wb_display_text(row, c, text, sizeof text);
                w_field(&w, text, delimiter);
            }
            w_byte(&w, '\r');       /* CRLF: what every spreadsheet writes */
            w_byte(&w, '\n');
            if (row == X16S_MAX_ROWS - 1)
                break;
        }
    }

    w_flush(&w);
    e = file_close(&w.f);
    return w.err != ERR_OK ? w.err : e;
}

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
