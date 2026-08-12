/* compile.c — formula text to bytecode.
 *
 * A single-pass recursive-descent parser that emits as it goes; there is no
 * token array and no syntax tree, because neither would fit anywhere useful.
 * Precedence climbing keeps the operator table to one small array.
 */
#include "formula.h"
/* XLSX_FFIELD: the layout the importer writes. */
#include "../import/xlsx_ctx.h"
#include "../workbook/workbook.h"
#include "../workbook/cells.h"
#include "../workbook/workbook_priv.h"
#include "../workbook/strings.h"
#include "../ui/menu.h"   /* sort_undo_* -- the permutation a sort left */
#include "../platform/banked_ram.h"
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY3")
#  pragma rodata-name (push, "OVL3RO")
#  pragma bss-name (push, "OVL3BSS")
#endif

/* Parser state. Static rather than passed around: cc65 charges for every
 * argument, and there is only ever one compilation in flight. Statics of an
 * overlay module live in resident bss, so this is deliberately small.
 *
 * The bytecode is emitted into banked RAM rather than a local array,
 * because cc65 caps a function's locals near 256 bytes and a 255-character
 * formula can compile to considerably more than that — a reference costs
 * four bytes for two characters typed, a literal number six for one. */
/* Zero page, not this overlay's bss: the parser touches these on nearly
 * every step, and each reference is a byte shorter here. Re-initialised at
 * the top of every compile, so it does not matter that zero page is not
 * reloaded with the overlay. */
X16S_ZP_BEGIN
static const char *src;         /* read position                          */
static uint16_t    out_n;
static err_t       perr;
X16S_ZP_END

static handle_t    out_h;       /* scratch block the bytecode grows in     */

static void emit(uint8_t b)
{
    if (out_n < FORMULA_MAX_CODE)
        bank_poke(out_h, out_n++, b);
    else
        perr = ERR_LIMIT;
}

static void emit16(uint16_t v)
{
    emit((uint8_t)v);
    emit((uint8_t)(v >> 8));
}

static void skip_spaces(void)
{
    while (*src == ' ')
        ++src;
}

#define IS_DIGIT(c) ((c) >= '0' && (c) <= '9')
#define IS_ALPHA(c) (((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z'))

static char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
}

/* --- references ----------------------------------------------------- */

/* Try to read A1-style column letters and a row number at `src`, allowing
 * the $ that marks an absolute reference. Absolute and relative parse the
 * same: nothing here copies or moves formulas yet, so the distinction has no
 * behaviour to attach to — but it has to be accepted, because Excel files
 * and users both write it.
 *
 * Returns 1 and advances on success; leaves `src` untouched on failure, so
 * the caller can try something else. */
static uint8_t parse_ref(uint16_t *row, uint8_t *col)
{
    const char *save = src;
    uint32_t c = 0;
    uint32_t r = 0;
    uint8_t letters = 0, digits = 0;

    if (*src == '$')
        ++src;
    while (IS_ALPHA(*src)) {
        if (++letters > 2)                   /* IV is the widest column */
            goto fail;
        c = c * 26 + (uint32_t)(upper(*src) - 'A' + 1);
        ++src;
    }
    if (!letters)
        goto fail;

    if (*src == '$')
        ++src;
    while (IS_DIGIT(*src)) {
        if (++digits > 5)
            goto fail;
        r = r * 10 + (uint32_t)(*src - '0');
        ++src;
    }
    if (!digits)
        goto fail;

    /* A cell name that is syntactically fine but off the sheet is #REF!,
     * not a syntax error — the distinction matters when importing. */
    if (c == 0 || c > X16S_MAX_COLS || r == 0 || r > X16S_MAX_ROWS) {
        perr = ERR_REF;
        return 0;
    }

    *col = (uint8_t)(c - 1);
    *row = (uint16_t)(r - 1);
    return 1;

fail:
    src = save;
    return 0;
}

/* --- sheet-qualified references --------------------------------------- *
 *
 * Sheet2!A1, Sheet2!A1:B9, and 'Some Name'!A1 for a name with a space or a
 * leading digit, which is how Excel writes it and so how it arrives from an
 * import.
 *
 * MUST BE TRIED BEFORE A PLAIN REFERENCE: a sheet name can look like a cell,
 * so a sheet called A1 would otherwise parse as a cell and leave the '!'
 * behind as a syntax error. The cost is one rewind when there is no '!'.
 *
 * The name comes straight out of the resident sheet table. wb_sheet_name()
 * is in another overlay, and wb_tab_name() truncates to the width of a tab,
 * which would fail to match a correctly spelled longer name. */
static uint8_t sheet_prefix(void)
{
    /* OVL3BSS -- see the pragma at the top of this file. Off the C stack
     * on purpose: this runs inside an .xlsx import, which is already the
     * deepest chain in the program. */
    static char sname[X16S_MAX_SHEET_NAME + 1];

    const char *save = src;
    const char *start;
    uint8_t len = 0, quoted = 0, i, j;

    if (*src == '\'') {
        quoted = 1;
        ++src;
    }
    start = src;

    while (*src && *src != '!'
           && (quoted ? *src != '\''
                      : (IS_ALPHA(*src) || IS_DIGIT(*src)
                         || *src == '_' || *src == '.'))) {
        ++src;
        ++len;
    }

    if (quoted) {
        if (*src != '\'')
            goto none;
        ++src;
    }
    if (len == 0 || *src != '!')
        goto none;
    ++src;

    for (i = 0; i < wb_sheet_n; ++i) {
        if (sheets == H_NULL)
            break;
        bank_read(sheets, (uint16_t)(slot_off(i) + SH_NAME), sname,
                  X16S_MAX_SHEET_NAME);
        sname[X16S_MAX_SHEET_NAME] = '\0';
        for (j = 0; j < len && sname[j]; ++j)
            if (upper(sname[j]) != upper(start[j]))
                break;
        if (j == len && sname[j] == '\0') {
            emit(OP_SHEET);
            emit(i);
            return 1;
        }
    }

    /* Spelled like a sheet reference but naming no sheet in this workbook.
     * That is #REF!, not a syntax error -- the same answer Excel gives, and
     * the distinction matters on import, where a workbook can refer to a
     * sheet we declined to read. */
    perr = ERR_REF;
    return 1;

none:
    src = save;
    return 0;
}

/* --- function names -------------------------------------------------- */

/* One packed array of NUL-separated names, walked rather than indexed.
 *
 * A table of pointers to literals costs a pointer each AND leaves the text
 * itself in resident memory: cc65 places string literals in RODATA whatever
 * rodata-name says, but honours the pragma for a named const array. This is
 * read only while the compiler overlay is loaded. */
static const char fn_names[] =
    "SUM\0" "AVERAGE\0" "MIN\0" "MAX\0" "COUNT\0"
    "IF\0" "AND\0" "OR\0" "NOT\0"
    "ABS\0" "ROUND\0" "INT\0" "MOD\0" "LEN\0";

/* Argument counts, as {min, max}. 255 means "any number", which is what the
 * aggregates need. */
static const uint8_t fn_args[FN__COUNT][2] = {
    {1,255}, {1,255}, {1,255}, {1,255}, {1,255},
    {2,3},   {1,255}, {1,255}, {1,1},
    {1,1},   {1,2},   {1,1},   {2,2},   {1,1}
};

static uint8_t lookup_fn(const char *name, uint8_t len)
{
    const char *n = fn_names;
    uint8_t i, j;

    for (i = 0; i < FN__COUNT; ++i) {
        for (j = 0; j < len && n[j]; ++j)
            if (upper(name[j]) != n[j])
                break;
        if (j == len && n[j] == '\0')
            return i;
        while (*n)              /* step over this name to the next */
            ++n;
        ++n;
    }
    return 0xFF;
}

/* --- expression parsing --------------------------------------------- */

static void parse_expr(void);

static void parse_primary(void)
{
    skip_spaces();

    if (*src == '(') {
        ++src;
        parse_expr();
        skip_spaces();
        if (*src == ')')
            ++src;
        else
            perr = ERR_SYNTAX;
        return;
    }

    if (*src == '-') {
        ++src;
        parse_primary();
        emit(OP_NEG);
        return;
    }
    if (*src == '+') {
        ++src;
        parse_primary();
        return;
    }

    if (*src == '"') {
        /* A literal string. Doubled quotes are an embedded quote. */
        char buf[48];
        uint8_t n = 0;
        uint16_t id;

        ++src;
        while (*src && n < sizeof buf) {
            if (*src == '"') {
                if (src[1] == '"') {
                    buf[n++] = '"';
                    src += 2;
                    continue;
                }
                break;
            }
            buf[n++] = *src++;
        }
        if (*src != '"') {
            perr = ERR_SYNTAX;
            return;
        }
        ++src;
        if (strpool_add(buf, n, &id) != ERR_OK) {
            perr = ERR_NOMEM;
            return;
        }
        emit(OP_PUSH_STR);
        emit16(id);
        return;
    }

    if (IS_DIGIT(*src) || (*src == '.' && IS_DIGIT(src[1]))) {
        char buf[24];
        uint8_t n = 0;
        snum_t v;

        while (n < sizeof buf - 1
               && (IS_DIGIT(*src) || *src == '.'
                   || *src == 'e' || *src == 'E'
                   || ((*src == '+' || *src == '-')
                       && n && (buf[n-1] == 'e' || buf[n-1] == 'E'))))
            buf[n++] = *src++;
        buf[n] = '\0';
        if (snum_parse(buf, &v) != ERR_OK) {
            perr = ERR_SYNTAX;
            return;
        }
        emit(OP_PUSH_NUM);
        {
            uint8_t i;
            for (i = 0; i < 5; ++i)
                emit(v.b[i]);
        }
        return;
    }

    if (IS_ALPHA(*src) || *src == '$' || *src == '\'') {
        const char *save = src;
        uint16_t r1, r2;
        uint8_t c1, c2;

        /* Sheet2! first, and if it was there the OP_SHEET prefix has been
         * emitted, so what follows must be a reference and cannot fall
         * through to the function-name branch below. */
        if (sheet_prefix()) {
            if (perr != ERR_OK)
                return;
            if (!parse_ref(&r1, &c1)) {
                if (perr == ERR_OK)
                    perr = ERR_SYNTAX;
                return;
            }
            if (*src == ':') {
                ++src;
                if (!parse_ref(&r2, &c2)) {
                    if (perr == ERR_OK)
                        perr = ERR_SYNTAX;
                    return;
                }
                if (r1 > r2) { uint16_t t = r1; r1 = r2; r2 = t; }
                if (c1 > c2) { uint8_t t = c1; c1 = c2; c2 = t; }
                emit(OP_PUSH_RANGE);
                emit16(r1); emit(c1);
                emit16(r2); emit(c2);
                return;
            }
            emit(OP_PUSH_CELL);
            emit16(r1); emit(c1);
            return;
        }
        if (perr != ERR_OK)
            return;

        /* A range first: A1:B20 is not two references and a colon. */
        if (parse_ref(&r1, &c1)) {
            if (*src == ':') {
                ++src;
                if (!parse_ref(&r2, &c2)) {
                    if (perr == ERR_OK)
                        perr = ERR_SYNTAX;
                    return;
                }
                /* Normalise, so B20:A1 means the same rectangle as A1:B20. */
                if (r1 > r2) { uint16_t t = r1; r1 = r2; r2 = t; }
                if (c1 > c2) { uint8_t t = c1; c1 = c2; c2 = t; }
                emit(OP_PUSH_RANGE);
                emit16(r1); emit(c1);
                emit16(r2); emit(c2);
                return;
            }
            emit(OP_PUSH_CELL);
            emit16(r1); emit(c1);
            return;
        }
        if (perr != ERR_OK)
            return;                          /* parse_ref reported #REF! */

        /* Not a reference, so a name: a function call, or TRUE/FALSE. */
        src = save;
        {
            const char *start = src;
            uint8_t len = 0, fn;

            while (IS_ALPHA(*src) || IS_DIGIT(*src) || *src == '.') {
                ++src;
                ++len;
            }
            if (len == 0) {
                perr = ERR_SYNTAX;
                return;
            }

            if (len == 4 && upper(start[0]) == 'T' && upper(start[1]) == 'R'
                && upper(start[2]) == 'U' && upper(start[3]) == 'E') {
                emit(OP_PUSH_BOOL);
                emit(1);
                return;
            }
            if (len == 5 && upper(start[0]) == 'F' && upper(start[1]) == 'A'
                && upper(start[2]) == 'L' && upper(start[3]) == 'S'
                && upper(start[4]) == 'E') {
                emit(OP_PUSH_BOOL);
                emit(0);
                return;
            }

            fn = lookup_fn(start, len);
            skip_spaces();
            if (fn == 0xFF || *src != '(') {
                perr = ERR_NAME;
                return;
            }
            ++src;

            {
                uint8_t argc = 0;
                skip_spaces();
                if (*src == ')') {
                    ++src;
                } else {
                    for (;;) {
                        parse_expr();
                        if (perr != ERR_OK)
                            return;
                        ++argc;
                        skip_spaces();
                        if (*src == ',') {
                            ++src;
                            continue;
                        }
                        if (*src == ')') {
                            ++src;
                            break;
                        }
                        perr = ERR_SYNTAX;
                        return;
                    }
                }
                if (argc < fn_args[fn][0]
                    || (fn_args[fn][1] != 255 && argc > fn_args[fn][1])) {
                    perr = ERR_VALUE;
                    return;
                }
                emit(OP_CALL);
                emit(fn);
                emit(argc);
            }
            return;
        }
    }

    perr = ERR_SYNTAX;
}

/* Exponentiation binds tighter than the binary operators and, as in Excel,
 * associates left to right. */
static void parse_power(void)
{
    parse_primary();
    for (;;) {
        skip_spaces();
        if (*src != '^' || perr != ERR_OK)
            return;
        ++src;
        parse_primary();
        emit(OP_POW);
    }
}

static void parse_term(void)
{
    parse_power();
    for (;;) {
        char c;
        skip_spaces();
        c = *src;
        if (perr != ERR_OK || (c != '*' && c != '/'))
            return;
        ++src;
        parse_power();
        emit(c == '*' ? OP_MUL : OP_DIV);
    }
}

static void parse_sum(void)
{
    parse_term();
    for (;;) {
        char c;
        skip_spaces();
        c = *src;
        if (perr != ERR_OK || (c != '+' && c != '-'))
            return;
        ++src;
        parse_term();
        emit(c == '+' ? OP_ADD : OP_SUB);
    }
}

/* Comparisons sit at the bottom and do not chain: =A1<B1<C1 is a syntax
 * error rather than something surprising. */
static void parse_expr(void)
{
    uint8_t op = 0;

    parse_sum();
    if (perr != ERR_OK)
        return;
    skip_spaces();

    if (src[0] == '<' && src[1] == '>') { op = OP_NE; src += 2; }
    else if (src[0] == '<' && src[1] == '=') { op = OP_LE; src += 2; }
    else if (src[0] == '>' && src[1] == '=') { op = OP_GE; src += 2; }
    else if (src[0] == '<') { op = OP_LT; ++src; }
    else if (src[0] == '>') { op = OP_GT; ++src; }
    else if (src[0] == '=') { op = OP_EQ; ++src; }
    else return;

    parse_sum();
    emit(op);
}

/* --- entry point ---------------------------------------------------- */

err_t formula_set(uint16_t row, uint16_t col, const char *text)
{
    cell_record_t rec;
    handle_t fr;
    uint16_t sid, len = 0, off;
    err_t e;

    while (text[len] && len < X16S_MAX_FORMULA_LEN + 1)
        ++len;
    if (len > X16S_MAX_FORMULA_LEN)
        return ERR_LIMIT;

    /* The scratch block is allocated and released per compilation rather
     * than kept in a static. A handle held across wb_reset() points into a
     * heap that no longer exists, and the next emit() would write through it
     * into whatever now occupies that block — silent corruption a long way
     * from its cause. One allocation per edit is not worth that risk. */
    out_h = bank_alloc(FORMULA_MAX_CODE);
    if (out_h == H_NULL)
        return ERR_NOMEM;

    src = text;
    if (*src == '=')
        ++src;
    out_n = 0;
    perr = ERR_OK;

    parse_expr();
    skip_spaces();
    if (perr == ERR_OK && *src != '\0')
        perr = ERR_SYNTAX;              /* trailing junk, e.g. "=1 2" */
    if (perr == ERR_OK)
        emit(OP_END);
    if (perr != ERR_OK) {
        bank_free(out_h);
        out_h = H_NULL;
        return perr;
    }

    /* Keep the source text: the formula bar shows it, the file stores it,
     * and an unsupported imported formula has nothing else to fall back on. */
    e = strpool_add(text, len, &sid);
    if (e != ERR_OK) {
        bank_free(out_h);
        out_h = H_NULL;
        return e;
    }

    fr = bank_alloc((uint16_t)(FR_CODE + out_n));
    if (fr == H_NULL) {
        strpool_unref(sid);
        bank_free(out_h);
        out_h = H_NULL;
        return ERR_NOMEM;
    }

    bank_poke(fr, FR_STATUS, FS_DIRTY);
    bank_poke(fr, FR_KIND, CELL_NUMBER);
    bank_write(fr, FR_VALUE, snum_zero.b, 5);
    bank_poke16(fr, FR_SRC, sid);
    bank_poke16(fr, FR_CODELEN, out_n);

    /* Scratch to record, in pieces small enough to sit on the stack. */
    for (off = 0; off < out_n; ) {
        uint8_t buf[48];
        uint16_t n = (uint16_t)(out_n - off);
        if (n > sizeof buf)
            n = sizeof buf;
        bank_read(out_h, off, buf, n);
        bank_write(fr, (uint16_t)(FR_CODE + off), buf, n);
        off = (uint16_t)(off + n);
    }
    bank_free(out_h);
    out_h = H_NULL;

    memset(&rec, 0, sizeof rec);
    rec.type = CELL_FORMULA;
    rec.val[0] = (uint8_t)fr;
    rec.val[1] = (uint8_t)(fr >> 8);
    rec.val[2] = (uint8_t)(fr >> 16);
    rec.val[3] = (uint8_t)(fr >> 24);

    return wb_set(row, col, &rec);
}

/* --- the importer's batch ---------------------------------------------- */

err_t formula_compile_batch(handle_t list, uint16_t n, uint16_t *compiled)
{
    /* Overlay bss: 256 bytes of the C stack is half of it, and this is the
     * longest formula the program accepts. */
    static char src_text[X16S_MAX_FORMULA_LEN + 2];
    uint16_t i, ok = 0;

    for (i = 0; i < n; ++i) {
        uint16_t off = (uint16_t)(i * XLSX_FFIELD);
        uint8_t  sht = bank_peek(list, off);
        uint16_t row = bank_peek16(list, (uint16_t)(off + 1));
        uint8_t  col = bank_peek(list, (uint16_t)(off + 3));
        uint16_t sid = bank_peek16(list, (uint16_t)(off + 4));

        /* The whole batch is compiled after the import, so the active sheet
         * is whichever one came last. Entries arrive in sheet order, so
         * this switches a handful of times rather than per formula. */
        if (sht != wb_sheet_i)
            wb_sheet_switch(sht);

        /* formula_set() builds a fresh cell, and a fresh cell has style 0 --
         * so compiling an imported formula threw away the format Excel had
         * given it. A SUM over a currency column came back as 3531.18
         * instead of $3531.18, and with the two-decimal format gone the MBF
         * mantissa showed through as 7.12999994. Carry the style over. */
        if (strpool_get(sid, src_text, sizeof src_text) != 0) {
            cell_record_t before, after;
            uint8_t had = wb_get(row, col, &before);

            if (formula_set(row, col, src_text) == ERR_OK) {
                if (had && before.style && wb_get(row, col, &after)) {
                    /* Straight into the store, NOT through wb_set().
                     *
                     * This puts a style byte back on a record that already
                     * exists, holding a handle the cell already owns.
                     * wb_set() would snapshot the cell for undo first, and
                     * a snapshot takes ownership -- leaving the undo slot
                     * and the live cell both owning one formula record, so
                     * the next edit frees a record the sheet is still
                     * using. It also keeps a bulk import out of the undo
                     * slot, as read_cells() does. */
                    after.style = before.style;
                    cells_set((cellstore_t *)wb_cells(), row, after.col,
                              &after);
                }
                ++ok;
            }
        }

        /* The parser took a reference so the text would survive until now;
         * formula_set() takes its own if it succeeded. */
        strpool_unref(sid);
    }

    if (compiled)
        *compiled = ok;
    return ERR_OK;
}

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
