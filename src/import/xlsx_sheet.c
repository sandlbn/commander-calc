#define BLOB_OWNER_SHEET
#include "blob_ovl.h"

#define XML_OWNER_SHEET
#include "xml_ovl.h"

#include "xlsx_sheet.h"
#include "../workbook/workbook.h"
#include "../workbook/strings.h"
#include "../platform/banked_ram.h"
#include "xlsx_ctx.h"
#include "../util/date.h"
#include "xml.h"
#include "../workbook/workbook.h"
#include "../workbook/strings.h"
#include "../platform/banked_ram.h"
#include "xlsx_ctx.h"
#include "../workbook/styles.h"
#include "../util/number.h"
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY10")
#  pragma rodata-name (push, "OVL10RO")
#  pragma bss-name (push, "OVL10BSS")
#endif

/* Named arrays, not literals: cc65 routes a string literal to
 * resident RODATA however rodata-name is set, and these are element
 * names only this overlay ever compares against. */
static const char E0[] = "c";
static const char E1[] = "v";
static const char E2[] = "t";
static const char E3[] = "col";
static const char E4[] = "f";
static const char E5[] = "min";
static const char E6[] = "max";
static const char E7[] = "width";
static const char E8[] = "r";
static const char E9[] = "s";

/* Too large for cc65's stack, and only one part is parsed at a time. */
static xml_t X;
/* One cell's value as written in the file. Half the cell text limit,
 * because this holds a number, a shared-string index or an inline string —
 * and the overlay it lives in is the tightest in the program. Anything
 * longer is truncated and counted. */
static char  text[512];

/* --- worksheet -------------------------------------------------------- */

/* "BC12" into a row and column. Returns 0 if it is not a reference, which
 * happens on the <row r="7"> attribute of the same name — same spelling,
 * different meaning, and the caller distinguishes them by element. */
static uint8_t parse_ref(const char *s, uint16_t *row, uint16_t *col)
{
    uint16_t c = 0, r = 0;
    uint8_t letters = 0;

    while (*s >= 'A' && *s <= 'Z') {
        c = (uint16_t)(c * 26 + (uint16_t)(*s - 'A' + 1));
        ++s;
        ++letters;
    }
    if (!letters || *s < '0' || *s > '9')
        return 0;
    while (*s >= '0' && *s <= '9')
        r = (uint16_t)(r * 10 + (uint16_t)(*s++ - '0'));
    if (*s || r == 0)
        return 0;

    *col = (uint16_t)(c - 1);       /* bijective base 26, so A is 1 */
    *row = (uint16_t)(r - 1);
    return 1;
}

static uint16_t to_u16(const char *s)
{
    uint16_t v = 0;

    while (*s >= '0' && *s <= '9')
        v = (uint16_t)(v * 10 + (uint16_t)(*s++ - '0'));
    return v;
}

/* Which of our styles a cell's xf index means. Adding rather than reusing:
 * two xfs with the same number format collapse to one style, which is what
 * styles_add is for. */
static uint8_t style_for(handle_t styles, uint8_t count, uint16_t xf)
{
    cell_style_t s;
    uint8_t id = 0;
    uint8_t fmt, places;

    if (!count || xf >= count)
        return 0;
    fmt = bank_peek(styles, XLSX_STYLE_FMT(xf));
    places = bank_peek(styles, XLSX_STYLE_PLACES(xf));
    if (fmt == NF_GENERAL && !(places & XLSX_PLACES_BOLD))
        return 0;                   /* style 0 is General already */

    memset(&s, 0, sizeof s);
    s.number_format = fmt;
    if (places & XLSX_PLACES_BOLD)
        s.flags = STY_BOLD;
    s.decimal_places = (uint8_t)(places & (uint8_t)~XLSX_PLACES_BOLD);
    if (styles_add(&s, &id) != ERR_OK)
        return 0;
    return id;
}

err_t xlsx_parse_sheet(blob_t *xml, handle_t ids, uint16_t id_count,
                       handle_t styles, uint8_t style_count,
                       xlsx_sheet_result_t *res)
{
    xml_token_t t;
    uint16_t row = 0, col = 0;
    uint16_t xf = 0;
    uint8_t  have_ref = 0, in_cell = 0, in_v = 0, in_is_t = 0, has_f = 0;
    uint16_t cmin = 0, cmax = 0; uint8_t cw = 0, in_col = 0;
    uint8_t  saw_t = 0, in_f = 0;
    char     kind = 'n';
    uint16_t text_len = 0;

    memset(res, 0, sizeof *res);
    blob_reset_read(xml);
    xml_init(&X, blob_feed, xml);

    while ((t = xml_next(&X)) != XML_EOF && t != XML_ERROR) {

        if (t == XML_START) {
            if (xml_is(&X, E0)) {
                in_cell = 1; have_ref = 0; has_f = 0; saw_t = 0;
                kind = 'n'; xf = 0;
                text[0] = '\0'; text_len = 0;
            } else if (in_cell && xml_is(&X, E1)) {
                in_v = 1; text_len = 0; text[0] = '\0';
            } else if (in_cell && xml_is(&X, E2)) {
                in_is_t = 1; saw_t = 1; text_len = 0; text[0] = '\0';
            } else if (xml_is(&X, E3)) {
                in_col = 1; cmin = cmax = 0; cw = 0;
            } else if (in_cell && xml_is(&X, E4)) {
                /* <f> comes before <v> in every cell, so the text buffer
                 * can be borrowed for the source and handed back before the
                 * cached value needs it. That is worth a little care: a
                 * second 64-byte buffer would not fit this overlay. */
                has_f = 1; in_f = 1; text_len = 0; text[0] = '\0';
            }
            continue;
        }

        if (t == XML_ATTR) {
            if (in_col) {
                if (xml_is(&X, E5))
                    cmin = to_u16(X.value);
                else if (xml_is(&X, E6))
                    cmax = to_u16(X.value);
                else if (xml_is(&X, E7))
                    /* "12.7109375" — the fraction is Excel measuring in
                     * proportional font units and we are drawing in a fixed
                     * one, so it could not be honoured even if there were a
                     * float type to hold it. to_u16 stops at the point. */
                    cw = (uint8_t)to_u16(X.value);
            } else if (in_cell) {
                if (xml_is(&X, E8))
                    have_ref = parse_ref(X.value, &row, &col);
                else if (xml_is(&X, E2))
                    /* The first letter, except that "s" and "str" share
                     * one: a shared-string index and a formula's text
                     * result are entirely different things. The switch below
                     * has always had a 'g' case for "str" — nothing could
                     * ever reach it, because this line handed it 's' and the
                     * text was read as an index into the shared table. */
                    kind = (X.value[0] == 's' && X.value[1] == 't')
                               ? 'g' : X.value[0];
                else if (xml_is(&X, E9))
                    xf = to_u16(X.value);
            }
            continue;
        }

        if (t == XML_TEXT) {
            if (in_v || in_is_t || in_f) {
                uint8_t k = 0;
                while (k < X.value_len && text_len < sizeof text - 1)
                    text[text_len++] = X.value[k++];
                text[text_len] = '\0';
            }
            continue;
        }

        /* XML_END */
        if (in_col) {
            /* <cols> is one entry per run of columns, and a run that sets
             * the sheet default covers all 16384 of them — so the loop is
             * bounded by what this program has rather than by what the file
             * claims. */
            in_col = 0;
            if (cw && cmin) {
                uint16_t c = (uint16_t)(cmin - 1);
                uint16_t last = cmax ? (uint16_t)(cmax - 1) : c;

                if (last > 255)
                    last = 255;
                for (; c <= last && c < 256; ++c)
                    wb_set_col_width(c, cw);
            }
            continue;
        }
        if (xml_is(&X, E1)) {
            in_v = 0;
            continue;
        }
        if (xml_is(&X, E2)) {
            in_is_t = 0;
            continue;
        }
        if (in_f && xml_is(&X, E4)) {
            /* Handed to the driver rather than recorded here: this overlay
             * has the XML tokenizer in it and 200 bytes of bookkeeping does
             * not fit beside that. Compiling happens after the import
             * anyway, in OVL_FCOMPILE — see xlsx_ctx.h. */
            in_f = 0;
            if (text_len && have_ref)
                xlsx_note_formula(row, col, text, text_len);
            text_len = 0;
            text[0] = '\0';
            continue;
        }
        if (!xml_is(&X, E0)) {
            continue;
        }

        /* End of a cell: store it. */
        in_cell = 0;
        /* An empty <c r="A1"/> holds nothing, but <is><t></t></is> is a
         * cell whose text is the empty string — which is a different thing
         * from an absent cell, and one Excel writes: DEMO.XLSX has one at
         * E18. Without saw_t it was silently dropped, and nothing noticed
         * until the same workbook was exported and read back. */
        if (!have_ref || (text_len == 0 && !saw_t))
            continue;

        if (row >= X16S_MAX_ROWS || col >= X16S_MAX_COLS) {
            res->truncated = 1;
            res->truncated = 1;
            continue;
        }

        {
            cell_record_t rec;
            err_t e = ERR_OK;

            memset(&rec, 0, sizeof rec);
            rec.col = (uint8_t)col;
            rec.style = style_for(styles, style_count, xf);

            switch (kind) {
            case 's': {                     /* shared string, by index */
                uint16_t idx = to_u16(text);
                uint16_t sid;
                if (idx >= id_count) {
                    res->truncated = 1;
                    continue;
                }
                sid = bank_peek16(ids, (uint16_t)(idx * 2));
                /* Take a reference. Every cell OWNS the string it names —
                 * that is the invariant the undo snapshot relies on when it
                 * inherits a cell's reference on overwrite. A shared string
                 * is named by many cells, and reusing the id from the
                 * shared table without referencing it leaves one reference
                 * covering all of them: the first cell to be replaced frees
                 * the text out from under every other cell that names it. */
                strpool_ref(sid);
                rec.type = CELL_TEXT;
                rec.val[0] = (uint8_t)sid;
                rec.val[1] = (uint8_t)(sid >> 8);
                break;
            }
            case 'b':                       /* boolean */
                rec.type = CELL_BOOLEAN;
                rec.val[0] = (uint8_t)(text[0] != '0');
                break;

            case 'e':                       /* an error Excel already had */
                rec.type = CELL_ERROR;
                rec.val[0] = (uint8_t)ERR_VALUE;
                if (text[1] == 'D') rec.val[0] = (uint8_t)ERR_DIVZERO;
                else if (text[1] == 'R') rec.val[0] = (uint8_t)ERR_REF;
                else if (text[1] == 'N') rec.val[0] = (uint8_t)ERR_NAME;
                break;

            case 'i':                       /* inlineStr */
            case 'g':                       /* str: a formula's text result */
            default:
                if (kind == 'i' || kind == 'g' || kind == 's') {
                    uint16_t id;
                    if (strpool_add(text, (uint16_t)text_len, &id) != ERR_OK) {
                        res->truncated = 1;
                        continue;
                    }
                    rec.type = CELL_TEXT;
                    rec.val[0] = (uint8_t)id;
                    rec.val[1] = (uint8_t)(id >> 8);
                } else {
                    /* A number, unless a style says it is a date. */
                    snum_t v;
                    if (snum_parse(text, &v) != ERR_OK) {
                        res->truncated = 1;
                        continue;
                    }
                    memcpy(rec.val, v.b, 5);
                    rec.type = CELL_NUMBER;
                    if (style_count && xf < style_count
                        && bank_peek(styles, XLSX_STYLE_FMT(xf)) == NF_DATE) {
                        rec.type = CELL_DATE;
                        ++res->dates;
                    }
                }
                break;
            }

            e = wb_set(row, col, &rec);
            if (e == ERR_LIMIT) {
                res->truncated = 1;
                continue;
            }
            if (e != ERR_OK)
                return e;

            ++res->cells;
            if (has_f)
                ++res->formulas;
        }
    }

    return ERR_OK;
}



#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
