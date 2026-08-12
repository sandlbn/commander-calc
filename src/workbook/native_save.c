#define FILEIO_OWNER_SAVE
#define CRC32_OWNER_SAVE
/* native_save.c — writing a .X16S workbook.
 *
 * A separate overlay from the reader in OVL_FILEIO: the two never run
 * together, and together they made that area the largest in the program.
 * The overlay window is sized by its largest tenant, so shrinking the
 * biggest one gives the resident image the difference.
 *
 * This overlay therefore carries its own copy of the file layer and of
 * CRC-32, as blob and xml do -- an overlay cannot call another overlay.
 */
#include "native_file.h"
#include "../formula/formula.h"
#include "workbook.h"
#include "strings.h"
#include "styles.h"
#include "cells.h"
#include "workbook_priv.h"
#include "../platform/banked_ram.h"
#include "../platform/file_io_ovl.h"
#include "../platform/file_io.h"
#include "../util/crc32_ovl.h"
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY11")
#  pragma rodata-name (push, "OVL11RO")
#endif

#define SHEET_NEW_NAME_ONLY
#include "sheet_new.c"          /* sheet_name_of(): see its top */

/* The row walkers, compiled into this overlay: they are not resident.
 * See cells_priv.h. Must be inside the pragma block above. */
#include "../workbook/cells_priv.h"
#include "../workbook/cells_iter.c"

/* Named arrays, not literals: cc65 leaves a literal in
 * resident RODATA however rodata-name is set. */
static const char S_0[] = "STYL";
static const char S_1[] = "STRS";
static const char S_2[] = "CELS";
static const char S_3[] = "X16S";
static const char S_4[] = "META";
static const char S_5[] = "END ";
static const char S_6[] = "COLW";
static const char S_7[] = "SHNM";

#define IO_BUF 64       /* small: this lives on the 2 KB C stack */

/* --- buffered writer with a running checksum ----------------------- */

typedef struct {
    fstream_t f;
    uint8_t   buf[IO_BUF];
    uint8_t   n;
    uint32_t  crc;
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

static void w_bytes(wr_t *w, const void *p, uint16_t len)
{
    const uint8_t *b = (const uint8_t *)p;

    if (w->err != ERR_OK)
        return;
    w->crc = crc32_update(w->crc, b, len);
    while (len--) {
        w->buf[w->n++] = *b++;
        if (w->n == IO_BUF)
            w_flush(w);
    }
}


static void w_u16(wr_t *w, uint16_t v)
{
    uint8_t b[2];
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    w_bytes(w, b, 2);
}

static void w_u32(wr_t *w, uint32_t v)
{
    uint8_t b[4];
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
    w_bytes(w, b, 4);
}

static void w_chunk_head(wr_t *w, const char *tag, uint32_t len)
{
    w_bytes(w, tag, 4);
    w_u32(w, len);
}

/* --- writing -------------------------------------------------------- */

/* Column widths, one byte each, all 256 of them.
 *
 * Its own chunk rather than more fields in META, because the reader skips
 * chunks it does not know: a file written by this version opens in the
 * previous one with default widths instead of failing its checksum, and a
 * file written by the previous one opens here the same way. 256 bytes is
 * not worth encoding as runs — the whole workbook is already larger than
 * that by the time it has a single row.
 */
/* The names, packed and NUL-separated, in sheet order. */
static void write_sheet_names(wr_t *w)
{
    char name[X16S_MAX_SHEET_NAME + 1];
    uint32_t len = 0;
    uint8_t i, n;

    for (i = 0; i < wb_sheet_n; ++i) {
        sheet_name_of(i, name);
        len += (uint32_t)strlen(name) + 1;
    }
    w_chunk_head(w, S_7, len);
    for (i = 0; i < wb_sheet_n; ++i) {
        sheet_name_of(i, name);
        n = (uint8_t)strlen(name);
        w_bytes(w, name, n);
        w_bytes(w, "", 1);
    }
}

static void write_col_widths(wr_t *w, uint8_t sheet)
{
    uint16_t c;

    w_chunk_head(w, S_6, 257);
    w_bytes(w, &sheet, 1);
    for (c = 0; c < 256; ++c) {
        uint8_t b = wb_col_width(c);
        w_bytes(w, &b, 1);
    }
}

static void write_styles(wr_t *w)
{
    uint8_t n = styles_count(), i;
    cell_style_t st;

    w_chunk_head(w, S_0, (uint32_t)(2 + (uint32_t)n * STYLE_SIZE));
    w_u16(w, n);
    for (i = 0; i < n; ++i) {
        styles_get(i, &st);
        w_bytes(w, &st, STYLE_SIZE);
    }
}

/* One string, streamed. The buffer is small and local on purpose: cc65
 * limits a function's locals to far less than the 1024-byte maximum string,
 * so nothing here may hold a whole one. */
static void write_one_string(wr_t *w, uint16_t id)
{
    char buf[48];
    uint16_t len = strpool_len(id), off = 0;

    w_u16(w, id);
    w_u16(w, len);
    while (off < len) {
        uint16_t n = strpool_read(id, off, buf, sizeof buf);
        if (n == 0)
            break;
        w_bytes(w, buf, n);
        off = (uint16_t)(off + n);
    }
}

static void write_strings(wr_t *w)
{
    uint16_t max = strpool_max_id(), id, n = 0;
    uint32_t len = 2;

    /* The payload length has to be known before the payload, and the pool
     * cannot report it, so measure first. Two passes over a few thousand
     * strings is cheaper than buffering them all in RAM we do not have. */
    for (id = 1; id <= max; ++id)
        if (strpool_alive(id)) {
            len += 4 + strpool_len(id);
            ++n;
        }

    /* The count written is the number this loop found, not
     * strpool_count(). They are not always the same -- importing three
     * worksheets leaves the pool with ids the count does not agree with --
     * and the reader takes this number as how many records follow. When it
     * was too small the reader stopped early, left the cursor in the
     * middle of the chunk, and read the rest of the file as garbage. */
    w_chunk_head(w, S_1, len);
    w_u16(w, n);
    for (id = 1; id <= max; ++id)
        if (strpool_alive(id))
            write_one_string(w, id);
}

static void write_cells(wr_t *w, uint8_t sheet)
{
    const cellstore_t *cs = wb_cells();
    uint32_t len = 4;
    uint16_t row;

    /* Same two-pass reason as the strings: measure, then emit. */
    for (row = cells_next_row(cs, 0); row < X16S_MAX_ROWS;
         row = cells_next_row(cs, (uint16_t)(row + 1))) {
        len += 4 + (uint32_t)cells_row_count(cs, row) * 8;
        if (row == X16S_MAX_ROWS - 1)
            break;
    }

    w_chunk_head(w, S_2, len + 1);
    w_bytes(w, &sheet, 1);
    w_u32(w, cs->cell_count);

    for (row = cells_next_row(cs, 0); row < X16S_MAX_ROWS;
         row = cells_next_row(cs, (uint16_t)(row + 1))) {
        uint16_t n = cells_row_count(cs, row), i;
        cell_record_t rec;

        w_u16(w, row);
        w_u16(w, n);
        for (i = 0; i < n; ++i) {
            cells_row_at(cs, row, i, &rec);

            /* A FORMULA CELL'S val[] IS A BANKED HANDLE, and a handle is
             * meaningless in a file: the heap it points into is rebuilt by
             * the next wb_reset(). Writing it verbatim is what made every
             * saved formula come back as a nonsense number.
             *
             * What goes in its place is the pool id of the source text,
             * which the STRS chunk has already persisted -- so the formula
             * travels as the thing the user typed, and the reader compiles
             * it again. Everything else about the cell, its style
             * especially, is unchanged. */
            if (rec.type == CELL_FORMULA) {
                handle_t fr = (handle_t)rec.val[0]
                            | ((handle_t)rec.val[1] << 8)
                            | ((handle_t)rec.val[2] << 16)
                            | ((handle_t)rec.val[3] << 24);
                uint16_t sid = bank_peek16(fr, FR_SRC);

                memset(rec.val, 0, sizeof rec.val);
                rec.val[0] = (uint8_t)sid;
                rec.val[1] = (uint8_t)(sid >> 8);
            }
            w_bytes(w, &rec, 8);
        }
        if (row == X16S_MAX_ROWS - 1)
            break;
    }
}

static err_t write_all(const char *name)
{
    wr_t w;
    const cellstore_t *cs = wb_cells();
    err_t e;

    memset(&w, 0, sizeof w);
    w.crc = crc32_init();

    e = file_open_write(&w.f, name);
    if (e != ERR_OK)
        return e;

    w_bytes(&w, S_3, 4);
    w_u16(&w, X16S_VERSION);
    w_u16(&w, 0);                       /* flags */

    w_chunk_head(&w, S_4, 8);
    w_u16(&w, wb_sheet_n);
    w_u16(&w, wb_sheet_i);
    w_u16(&w, cs->max_row);             /* of the active sheet, as before */
    w_u16(&w, cs->max_col);

    write_sheet_names(&w);
    write_styles(&w);
    write_strings(&w);

    /* Every sheet's cells and widths, each chunk saying which sheet it is
     * for. The styles and the string pool are the workbook's and are
     * written once, above. */
    {
        uint8_t here = wb_sheet_i, i;

        for (i = 0; i < wb_sheet_n; ++i) {
            wb_sheet_switch(i);
            write_col_widths(&w, i);
            write_cells(&w, i);
        }
        wb_sheet_switch(here);
    }

    w_chunk_head(&w, S_5, 0);

    /* The checksum covers everything before itself, so it is written after
     * the running value stops changing. */
    {
        uint32_t crc = crc32_final(w.crc);
        uint8_t b[4];
        b[0] = (uint8_t)crc;
        b[1] = (uint8_t)(crc >> 8);
        b[2] = (uint8_t)(crc >> 16);
        b[3] = (uint8_t)(crc >> 24);
        if (w.err == ERR_OK) {
            /* Bypass w_bytes: the CRC must not include itself. */
            uint8_t i;
            for (i = 0; i < 4; ++i) {
                w.buf[w.n++] = b[i];
                if (w.n == IO_BUF)
                    w_flush(&w);
            }
        }
    }
    w_flush(&w);

    e = file_close(&w.f);
    return w.err != ERR_OK ? w.err : e;
}

/* Read a file back and confirm the checksum. Cheap insurance: a save that
 * silently half-wrote is the one failure a spreadsheet must never have. */
static err_t verify(const char *name)
{
    fstream_t f;
    uint8_t buf[IO_BUF];
    uint32_t crc = crc32_init();
    uint8_t tail[4];
    uint8_t have = 0;
    err_t e;

    e = file_open_read(&f, name);
    if (e != ERR_OK)
        return e;

    /* Everything except the last four bytes feeds the checksum, and those
     * last four are not known to be last until the read runs dry. */
    for (;;) {
        uint16_t got = file_read(&f, buf, IO_BUF);
        uint16_t i;
        if (got == 0)
            break;
        for (i = 0; i < got; ++i) {
            if (have == 4) {
                crc = crc32_update(crc, tail, 1);
                tail[0] = tail[1];
                tail[1] = tail[2];
                tail[2] = tail[3];
                --have;
            }
            tail[have++] = buf[i];
        }
        if (file_eof(&f))
            break;
    }
    file_close(&f);

    if (have < 4)
        return ERR_BADFORMAT;

    {
        uint32_t want = (uint32_t)tail[0] | ((uint32_t)tail[1] << 8)
                      | ((uint32_t)tail[2] << 16) | ((uint32_t)tail[3] << 24);
        return crc32_final(crc) == want ? ERR_OK : ERR_CHECKSUM;
    }
}

err_t x16s_save(const char *name)
{
    err_t e = write_all(X16S_TMP);

    if (e != ERR_OK) {
        file_remove(X16S_TMP);
        return e;
    }

    e = verify(X16S_TMP);
    if (e != ERR_OK) {
        file_remove(X16S_TMP);
        return e;
    }

    /* Only now is the previous file expendable. */
    file_remove(name);                  /* absent is fine */
    e = file_rename(X16S_TMP, name);
    if (e != ERR_OK) {
        file_remove(X16S_TMP);
        return e;
    }

    (wb_dirty = 0);
    return ERR_OK;
}

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
