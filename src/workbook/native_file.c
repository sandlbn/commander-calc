/* native_file.c — reading a .X16S.
 *
 * The writer is in native_save.c and its own overlay; see the note there for
 * why. The chunk tags below are duplicated rather than shared for the same
 * reason: 30 bytes in each of two overlays against a resident copy.
 */
#include "native_file.h"
#include "../formula/formula.h"
#include "workbook.h"
#include "strings.h"
#include "styles.h"
#include "cells.h"
#include "workbook_priv.h"
#include "../platform/banked_ram.h"
#include "../platform/file_io.h"
#include "../util/crc32.h"
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY2")
#  pragma rodata-name (push, "OVL2RO")
#endif

#include "sheet_new.c"          /* sheet_new(), and the names */

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

/* --- buffered reader with the same checksum ------------------------ */

typedef struct {
    fstream_t f;
    uint8_t   buf[IO_BUF];
    uint8_t   n, pos;
    uint32_t  crc;
    err_t     err;
} rd_t;

static uint8_t r_u8(rd_t *r)
{
    uint8_t v;

    if (r->err != ERR_OK)
        return 0;
    if (r->pos == r->n) {
        r->n = (uint8_t)file_read(&r->f, r->buf, IO_BUF);
        r->pos = 0;
        if (r->n == 0) {
            r->err = ERR_EOF;
            return 0;
        }
    }
    v = r->buf[r->pos++];
    r->crc = crc32_update(r->crc, &v, 1);
    return v;
}

static void r_bytes(rd_t *r, void *p, uint16_t len)
{
    uint8_t *b = (uint8_t *)p;

    while (len--)
        *b++ = r_u8(r);
}

static uint16_t r_u16(rd_t *r)
{
    uint16_t lo = r_u8(r);
    return (uint16_t)(lo | ((uint16_t)r_u8(r) << 8));
}

static uint32_t r_u32(rd_t *r)
{
    uint32_t v = r_u16(r);
    return v | ((uint32_t)r_u16(r) << 16);
}

/* --- reading -------------------------------------------------------- */

static void skip(rd_t *r, uint32_t len);

/* Which format this file is. Version 1 held one unnamed sheet and its COLW
 * and CELS chunks carry no sheet number; version 2 puts one in front of
 * each. Both still open. */
/* Golden RAM: these are statics of an overlay module, and the pragma block
 * at the top of this file does not push bss-name, so they would otherwise
 * sit in the resident image. */
X16S_GOLDEN_BEGIN
static uint8_t file_version;
static uint8_t name_taken;
X16S_GOLDEN_END

/* The names, in sheet order. The first renames the sheet wb_reset() made;
 * the rest are created. */
static err_t read_sheet_names(rd_t *r, uint32_t len)
{
    char name[X16S_MAX_SHEET_NAME + 1];
    uint8_t n;

    while (len && r->err == ERR_OK) {
        n = 0;
        for (;;) {
            uint8_t c = r_u8(r);
            if (len)
                --len;
            if (c == 0 || r->err != ERR_OK)
                break;
            if (n < X16S_MAX_SHEET_NAME)
                name[n++] = (char)c;
        }
        name[n] = '\0';
        if (n == 0)
            continue;
        if (wb_sheet_n == 1 && wb_sheet_i == 0 && !name_taken) {
            sheet_set_name(0, name);
            name_taken = 1;
        } else if (sheet_new(name) != ERR_OK) {
            return ERR_LIMIT;           /* more sheets than we hold */
        }
    }
    return r->err;
}

/* Which sheet the chunk that follows belongs to. Version 1 files have no
 * such byte and everything belongs to the only sheet there is. */
static err_t chunk_sheet(rd_t *r, uint32_t *len)
{
    uint8_t n;

    if (file_version < 2)
        return ERR_OK;
    n = r_u8(r);
    if (*len)
        --*len;
    return wb_sheet_switch(n);
}

/* A short chunk is read as far as it goes and the rest keep their defaults;
 * a long one has its tail skipped. Neither is worth refusing a file over,
 * and both are what a version that stored a different number of columns
 * would look like from here. */
static err_t read_col_widths(rd_t *r, uint32_t len)
{
    uint16_t c;

    for (c = 0; c < 256 && (uint32_t)c < len; ++c)
        wb_set_col_width(c, r_u8(r));
    if (len > 256)
        skip(r, len - 256);
    return r->err;
}

static err_t read_styles(rd_t *r, uint32_t len)
{
    uint16_t n = r_u16(r), i;
    cell_style_t st;

    if ((uint32_t)2 + (uint32_t)n * STYLE_SIZE != len)
        return ERR_BADFORMAT;
    if (n > X16S_MAX_STYLES)
        return ERR_LIMIT;

    /* styles_init already made style 0; the file supplies it again. */
    for (i = 0; i < n; ++i) {
        r_bytes(r, &st, STYLE_SIZE);
        if (i == 0)
            continue;                   /* keep the default already present */
        if (styles_append(&st) != ERR_OK)
            return ERR_LIMIT;
    }
    return r->err;
}

static err_t read_one_string(rd_t *r, uint16_t id, uint16_t len)
{
    char buf[48];
    uint16_t off = 0;
    err_t e = strpool_load_begin(id, len);

    if (e != ERR_OK)
        return e;
    while (off < len) {
        uint16_t n = (uint16_t)(len - off);
        if (n > sizeof buf)
            n = sizeof buf;
        r_bytes(r, buf, n);
        if (r->err != ERR_OK)
            return r->err;
        strpool_load_chunk(id, off, buf, n);
        off = (uint16_t)(off + n);
    }
    return ERR_OK;
}

static err_t read_strings(rd_t *r, uint32_t len)
{
    uint16_t count = r_u16(r), i;

    (void)len;
    for (i = 0; i < count; ++i) {
        uint16_t id = r_u16(r);
        uint16_t n  = r_u16(r);
        err_t e;

        if (r->err != ERR_OK)
            return r->err;
        if (n > X16S_MAX_TEXT_LEN)
            return ERR_BADFORMAT;
        e = read_one_string(r, id, n);
        if (e != ERR_OK)
            return e;
    }
    return r->err;
}

/* One formula, put by for the compiler.
 *
 * chunk_sheet() has already switched to the sheet this chunk belongs to, so
 * wb_sheet_i is the right answer and does not have to be threaded through.
 * The list is compiled by formula_compile_pending() once this overlay is
 * out of the way -- the compiler is in another one. */
static void note_formula(uint16_t sid, uint16_t row, uint8_t col)
{
    uint8_t ent[FPEND_FIELD];

    if (formula_pending == H_NULL || formula_pending_n >= FPEND_MAX)
        return;

    ent[0] = wb_sheet_i;
    ent[1] = (uint8_t)row;
    ent[2] = (uint8_t)(row >> 8);
    ent[3] = col;
    ent[4] = (uint8_t)sid;
    ent[5] = (uint8_t)(sid >> 8);
    bank_write(formula_pending, (uint16_t)(formula_pending_n * FPEND_FIELD),
               ent, FPEND_FIELD);
    ++formula_pending_n;
}

static err_t read_cells(rd_t *r, uint32_t len)
{
    uint32_t total = r_u32(r), done = 0;

    (void)len;
    while (done < total) {
        uint16_t row = r_u16(r);
        uint16_t n   = r_u16(r), i;

        if (r->err != ERR_OK)
            return r->err;
        if (n == 0 || n > X16S_MAX_COLS)
            return ERR_BADFORMAT;

        for (i = 0; i < n; ++i) {
            cell_record_t rec;
            err_t e;

            r_bytes(r, &rec, 8);
            if (r->err != ERR_OK)
                return r->err;

            /* A formula arrives as the pool id of its source text (see
             * write_cells()). Collect it for the compiler, which runs after
             * the file is closed because it lives in another overlay, and
             * clear val[] so nothing can mistake the remains for a handle.
             *
             * A version 2 file holds a real banked handle here, which
             * cannot be recovered; those formulas become empty cells
             * rather than the nonsense the dangling handle would read as. */
            if (rec.type == CELL_FORMULA) {
                if (file_version < 3) {
                    /* An older file has the banked handle here, and a
                     * handle from a previous run addresses nothing. There
                     * is nothing to rebuild the formula from, so the cell
                     * is left out rather than stored as one that would read
                     * as a nonsense number. Counted, or the chunk never
                     * ends. */
                    ++done;
                    continue;
                }
                note_formula((uint16_t)(rec.val[0]
                                        | ((uint16_t)rec.val[1] << 8)),
                             row, rec.col);
                /* Cleared so nothing can read what is left as a handle
                 * before the compiler rebuilds the cell. */
                memset(rec.val, 0, sizeof rec.val);
            }
            /* Straight into the store, not through wb_set: the file's
             * strings already carry their own reference, and the undo slot
             * must not fill up with the whole workbook. */
            e = cells_set((cellstore_t *)wb_cells(), row, rec.col, &rec);
            if (e != ERR_OK)
                return e;
            ++done;
        }
    }
    return r->err;
}

/* Consume a chunk this version does not know about. Skipping rather than
 * failing is the reason the format is chunked at all. */
static void skip(rd_t *r, uint32_t len)
{
    while (len-- && r->err == ERR_OK)
        (void)r_u8(r);
}

err_t x16s_load(const char *name)
{
    rd_t r;
    char tag[5];
    uint16_t version;
    err_t e;

    memset(&r, 0, sizeof r);
    r.crc = crc32_init();

    e = file_open_read(&r.f, name);
    if (e != ERR_OK)
        return e;

    r_bytes(&r, tag, 4);
    if (r.err != ERR_OK || memcmp(tag, S_3, 4) != 0) {
        file_close(&r.f);
        return ERR_BADFORMAT;
    }
    version = r_u16(&r);
    file_version = (uint8_t)version;
    name_taken = 0;

    /* The queue is left empty across the reset below. wb_reset() throws the
     * whole bank heap away, so a handle taken before it addresses memory
     * the incoming workbook is about to be given. */
    formula_pending_n = 0;
    formula_pending = H_NULL;
    (void)r_u16(&r);                    /* flags */
    if (version > X16S_VERSION) {
        file_close(&r.f);
        return ERR_BADFORMAT;
    }

    /* Nothing has been touched yet, so a rejected header leaves the current
     * workbook alone. Past this point the old one is gone. */
    e = wb_reset();
    if (e != ERR_OK) {
        file_close(&r.f);
        return e;
    }

    /* Somewhere to put the formulas as they go past, taken from the heap
     * the reset has just laid out. Released by formula_compile_pending(),
     * which the caller must invoke because compiling needs an overlay this
     * one cannot load. A failure to allocate is not a failure to load: the
     * cells still arrive, the formulas among them simply do not come back
     * as formulas. */
    formula_pending = bank_alloc(FPEND_MAX * FPEND_FIELD);

    for (;;) {
        uint32_t len;

        r_bytes(&r, tag, 4);
        len = r_u32(&r);
        if (r.err != ERR_OK) {
            e = ERR_EOF;
            break;
        }
        if (memcmp(tag, S_5, 4) == 0) {
            e = ERR_OK;
            break;
        }

        if (memcmp(tag, S_4, 4) == 0) {
            skip(&r, len);              /* single sheet until step 7 */
            e = r.err;
        } else if (memcmp(tag, S_0, 4) == 0) {
            e = read_styles(&r, len);
        } else if (memcmp(tag, S_1, 4) == 0) {
            e = read_strings(&r, len);
        } else if (memcmp(tag, S_2, 4) == 0) {
            e = chunk_sheet(&r, &len);
            if (e == ERR_OK)
                e = read_cells(&r, len);
        } else if (memcmp(tag, S_7, 4) == 0) {
            e = read_sheet_names(&r, len);
        } else if (memcmp(tag, S_6, 4) == 0) {
            e = chunk_sheet(&r, &len);
            if (e == ERR_OK)
                e = read_col_widths(&r, len);
        } else {
            skip(&r, len);
            e = r.err;
        }
        if (e != ERR_OK)
            break;
    }

    if (e == ERR_OK) {
        uint32_t want;
        uint8_t b[4];

        /* The trailing checksum is not part of the checksum, so read it
         * without letting r_u8 fold it in. */
        {
            uint32_t saved = r.crc;
            r_bytes(&r, b, 4);
            r.crc = saved;
        }
        want = (uint32_t)b[0] | ((uint32_t)b[1] << 8)
             | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
        if (r.err != ERR_OK)
            e = ERR_EOF;
        else if (crc32_final(r.crc) != want)
            e = ERR_CHECKSUM;
    }

    file_close(&r.f);

    if (e != ERR_OK) {
        /* A damaged file must not leave a half-built workbook on screen. */
        wb_reset();
        return e;
    }

    (wb_dirty = 0);
    return ERR_OK;
}

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
