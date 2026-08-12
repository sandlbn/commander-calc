/* eval.c — running the bytecode, and recalculating the sheet.
 *
 * A stack machine. The interesting part is the scheduling: with no
 * dependency graph, evaluation order is discovered by repeated passes.
 * Every formula starts dirty, each pass evaluates the ones whose inputs are
 * all clean, and a pass that makes no progress means the rest can only be
 * waiting on each other -- so circular references are detected by the
 * scheduler rather than by a separate graph walk.
 *
 * DO NOT SPLIT THIS OVERLAY. It fills OVL4 and looks like the obvious place
 * to reclaim space from, because the resident snum_* arithmetic has no other
 * caller. Both possible cuts have been built and measured and both are
 * worse; docs/design/formulas.md records by how much.
 */
#include "formula.h"
#include "../workbook/workbook.h"
#include "../workbook/cells.h"
#include "../workbook/strings.h"
#include "../platform/banked_ram.h"
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY4")
#  pragma rodata-name (push, "OVL4RO")
#endif

/* The row walkers, compiled into this overlay: they are not resident.
 * See cells_priv.h. Must be inside the pragma block above. */
#include "../workbook/cells_priv.h"
#include "../workbook/cells_iter.c"

#define STACK_MAX 10

enum { FV_NUM = 0, FV_STR, FV_BOOL, FV_ERR, FV_RANGE };

/* A string id and a range are never both meaningful, so they share space.
 * At ten deep the stack is 130 bytes rather than 180, which matters when
 * cc65 caps a function's locals near 256. */
typedef struct {
    uint8_t  kind;
    uint8_t  err;
    snum_t   num;
    union {
        uint16_t sid;
        struct { uint16_t r1, r2; uint8_t c1, c2; } rng;
    } u;
} fval_t;

/* Which sheets had a formula on them the last time every cell was looked
 * at. One byte a sheet, in golden RAM, so this costs the resident image
 * nothing at all. See each_formula(). */
X16S_GOLDEN_BEGIN
static uint8_t sheet_has_formulas[X16S_MAX_SHEETS];
X16S_GOLDEN_END

/* Set when evaluation touches a formula that has not been computed yet.
 * The scheduler reads it to decide whether to keep a result. */
X16S_GOLDEN_BEGIN
static uint8_t pending;
X16S_GOLDEN_END

/* --- reading another sheet -------------------------------------------- *
 *
 * =Sheet2!A1. The bytecode names the sheet with an OP_SHEET prefix, and this
 * is where the prefix is held between reading it and the push it applies to.
 * SH_HERE rather than 0 means "no prefix", because 0 is a real sheet.
 *
 * A foreign cell is read by making its sheet current and putting the old one
 * back, so a range costs one switch each way however many cells it covers.
 * See docs/design/formulas.md. */
#define SH_HERE 0xFF
X16S_GOLDEN_BEGIN
static uint8_t ref_sheet;
X16S_GOLDEN_END

static uint8_t sheet_enter(uint8_t s)
{
    uint8_t home = wb_sheet_i;

    /* No `s != home` test: wb_sheet_switch() already returns without doing
     * anything when asked for the sheet that is current. */
    if (s != SH_HERE)
        wb_sheet_switch(s);
    return home;
}

/* And leaving is just switching back, so it does not need a function. */
#define sheet_leave(home) wb_sheet_switch(home)

static handle_t formula_rec(const cell_record_t *rec)
{
    return (handle_t)rec->val[0]
         | ((handle_t)rec->val[1] << 8)
         | ((handle_t)rec->val[2] << 16)
         | ((handle_t)rec->val[3] << 24);
}

/* --- reading a cell as a value -------------------------------------- */

/* Read a cell into `v`; returns whether the cell exists.
 *
 * An absent cell reads as zero, which is what a spreadsheet wants everywhere
 * except COUNT and AVERAGE, where an empty cell is nothing at all rather
 * than a zero. The return value is how those two tell the difference
 * without a second lookup per cell. */
static uint8_t load_cell(uint16_t row, uint16_t col, fval_t *v)
{
    cell_record_t rec;

    /* No `v->num = snum_zero` after this: MBF zero is five zero bytes, so
     * the memset has already written it. It is a five-byte struct copy on
     * every cell of every range. */
    memset(v, 0, sizeof *v);
    v->kind = FV_NUM;

    if (!wb_get(row, col, &rec))
        return 0;                            /* empty cells count as zero */

    switch (rec.type) {
    case CELL_NUMBER:
    case CELL_DATE:
        memcpy(v->num.b, rec.val, 5);
        return 1;

    case CELL_TEXT:
        v->kind = FV_STR;
        v->u.sid = (uint16_t)(rec.val[0] | ((uint16_t)rec.val[1] << 8));
        return 1;

    case CELL_BOOLEAN:
        v->kind = FV_BOOL;
        snum_from_i16(&v->num, rec.val[0] ? 1 : 0);
        return 1;

    case CELL_ERROR:
        v->kind = FV_ERR;
        v->err = rec.val[0];
        return 1;

    case CELL_FORMULA: {
        handle_t fr = formula_rec(&rec);
        if (bank_peek(fr, FR_STATUS) == FS_DIRTY) {
            /* Not computed yet. Say so and let the scheduler come back. */
            pending = 1;
            v->kind = FV_ERR;
            v->err = ERR_VALUE;
            return 1;
        }
        v->kind = bank_peek(fr, FR_KIND);
        switch (v->kind) {
        case CELL_ERROR:
            v->kind = FV_ERR;
            v->err = bank_peek(fr, FR_VALUE);
            break;
        case CELL_TEXT:
            v->kind = FV_STR;
            v->u.sid = bank_peek16(fr, FR_VALUE);
            break;
        case CELL_BOOLEAN:
            v->kind = FV_BOOL;
            bank_read(fr, FR_VALUE, v->num.b, 5);
            break;
        default:
            v->kind = FV_NUM;
            bank_read(fr, FR_VALUE, v->num.b, 5);
            break;
        }
        return 1;
    }

    default:
        return 1;
    }
}

/* The same, on whichever sheet `s` names. Separate from load_cell() so the
 * ordinary same-sheet reference -- which is nearly all of them -- does not
 * carry the switch around with it. */
static uint8_t load_cell_on(uint8_t s, uint16_t row, uint16_t col, fval_t *v)
{
    uint8_t home = sheet_enter(s);
    uint8_t got  = load_cell(row, col, v);

    sheet_leave(home);
    return got;
}

/* Coerce to a number the way a spreadsheet does: booleans are 1 and 0, text
 * is a #VALUE! error rather than a silent zero. */
static uint8_t as_number(fval_t *v)
{
    if (v->kind == FV_NUM || v->kind == FV_BOOL)
        return 1;
    if (v->kind == FV_ERR)
        return 0;
    v->kind = FV_ERR;
    v->err = ERR_VALUE;
    return 0;
}

static void set_err(fval_t *v, err_t e)
{
    v->kind = FV_ERR;
    v->err = (uint8_t)e;
}

/* --- aggregation over arguments and ranges --------------------------- */

typedef struct {
    snum_t  acc;        /* running total, min or max                     */
    uint16_t count;     /* numeric values seen                           */
    uint8_t  started;
    uint8_t  err;
    uint8_t  mode;      /* 0 sum, 1 min, 2 max, 3 count                  */
} agg_t;

static void agg_one(agg_t *a, const snum_t *n)
{
    switch (a->mode) {
    case 0:
        if (snum_add(&a->acc, n) != ERR_OK)
            a->err = ERR_NUM;
        break;
    case 1:
        if (!a->started || snum_cmp(n, &a->acc) < 0)
            a->acc = *n;
        break;
    case 2:
        if (!a->started || snum_cmp(n, &a->acc) > 0)
            a->acc = *n;
        break;
    default:
        break;
    }
    a->started = 1;
    ++a->count;
}

/* Fold one argument in. A range contributes every numeric cell inside it;
 * text and empty cells are skipped, which is what makes COUNT and AVERAGE
 * agree with a spreadsheet rather than with an array language. */
static void agg_value(agg_t *a, const fval_t *v)
{
    if (a->err)
        return;

    if (v->kind == FV_ERR) {
        a->err = v->err ? v->err : (uint8_t)ERR_VALUE;
        return;
    }

    if (v->kind == FV_RANGE) {
        /* Entered once for the whole rectangle rather than per cell: see
         * the note on sheet_enter(). For a range on this sheet it does
         * nothing at all. */
        uint8_t home = sheet_enter(v->err);
        uint16_t r;

        /* Both loops stop on a->err rather than jumping out.
         *
         * NO `goto` OUT OF THIS BLOCK. cc65 does not unwind the C stack for
         * a goto leaving a block that holds a local, and `cv` is one: the
         * jump leaves c_sp a few bytes wrong and every local in every frame
         * above then reads from the wrong place. A `return` is fine.
         * See docs/design/platform.md. */
        for (r = v->u.rng.r1; r <= v->u.rng.r2 && !a->err; ++r) {
            uint16_t c;
            for (c = v->u.rng.c1; c <= v->u.rng.c2 && !a->err; ++c) {
                fval_t cv;
                /* The return value is the existence test: only cells that
                 * are really there, and really hold a number, take part,
                 * which is what makes COUNT and AVERAGE skip the gaps. */
                uint8_t got = load_cell(r, c, &cv);
                if (cv.kind == FV_ERR)
                    a->err = cv.err ? cv.err : (uint8_t)ERR_VALUE;
                else if (got && (cv.kind == FV_NUM || cv.kind == FV_BOOL))
                    agg_one(a, &cv.num);
            }
            if (v->u.rng.r2 == X16S_MAX_ROWS - 1 && r == v->u.rng.r2)
                break;
        }

        /* One place that puts the sheet back, and now every path reaches
         * it by falling out of the loops. */
        sheet_leave(home);
        return;
    }

    if (v->kind == FV_STR)
        return;                              /* a bare text argument is skipped */

    agg_one(a, &v->num);
}

/* --- the function library -------------------------------------------- */

static void call_fn(uint8_t fn, fval_t *args, uint8_t argc, fval_t *out)
{
    uint8_t i;

    memset(out, 0, sizeof *out);
    out->kind = FV_NUM;
    out->num = snum_zero;

    /* An error anywhere in the arguments propagates, except for IF, which
     * must be able to choose the branch that is not broken. */
    if (fn != FN_IF) {
        for (i = 0; i < argc; ++i)
            if (args[i].kind == FV_ERR) {
                *out = args[i];
                return;
            }
    }

    switch (fn) {
    case FN_SUM:
    case FN_AVERAGE:
    case FN_MIN:
    case FN_MAX:
    case FN_COUNT: {
        agg_t a;
        memset(&a, 0, sizeof a);
        a.acc = snum_zero;
        a.mode = (fn == FN_MIN) ? 1 : (fn == FN_MAX) ? 2
               : (fn == FN_COUNT) ? 3 : 0;
        for (i = 0; i < argc; ++i)
            agg_value(&a, &args[i]);
        if (a.err) {
            set_err(out, (err_t)a.err);
            return;
        }
        if (fn == FN_COUNT) {
            snum_from_i16(&out->num, (int16_t)a.count);
            return;
        }
        if (!a.started) {
            /* MIN and MAX of nothing are 0, matching Excel; AVERAGE of
             * nothing is #DIV/0!, also matching Excel. */
            if (fn == FN_AVERAGE)
                set_err(out, ERR_DIVZERO);
            return;
        }
        out->num = a.acc;
        if (fn == FN_AVERAGE) {
            snum_t n;
            snum_from_i16(&n, (int16_t)a.count);
            if (snum_div(&out->num, &n) != ERR_OK)
                set_err(out, ERR_DIVZERO);
        }
        return;
    }

    case FN_IF: {
        uint8_t truth;
        if (args[0].kind == FV_ERR) {
            *out = args[0];
            return;
        }
        if (args[0].kind == FV_STR) {
            set_err(out, ERR_VALUE);
            return;
        }
        truth = !snum_is_zero((&args[0].num));
        if (truth)
            *out = args[1];
        else if (argc >= 3)
            *out = args[2];
        else
            out->kind = FV_BOOL;             /* IF(false, x) is FALSE */
        return;
    }

    case FN_AND:
    case FN_OR: {
        uint8_t any = (fn == FN_OR) ? 0 : 1;
        for (i = 0; i < argc; ++i) {
            uint8_t t;
            if (!as_number(&args[i])) {
                *out = args[i];
                return;
            }
            t = !snum_is_zero((&args[i].num));
            if (fn == FN_AND)
                any = any && t;
            else
                any = any || t;
        }
        out->kind = FV_BOOL;
        snum_from_i16(&out->num, any ? 1 : 0);
        return;
    }

    case FN_NOT:
        if (!as_number(&args[0])) { *out = args[0]; return; }
        out->kind = FV_BOOL;
        snum_from_i16(&out->num, snum_is_zero((&args[0].num)) ? 1 : 0);
        return;

    case FN_ABS:
        if (!as_number(&args[0])) { *out = args[0]; return; }
        out->num = args[0].num;
        snum_abs(&out->num);
        return;

    case FN_INT:
        if (!as_number(&args[0])) { *out = args[0]; return; }
        out->num = args[0].num;
        snum_int(&out->num);
        return;

    case FN_ROUND: {
        int32_t places = 0;
        if (!as_number(&args[0])) { *out = args[0]; return; }
        if (argc >= 2) {
            if (!as_number(&args[1])) { *out = args[1]; return; }
            if (snum_to_i32(&args[1].num, &places) != ERR_OK
                || places < -9 || places > 9) {
                set_err(out, ERR_NUM);
                return;
            }
        }
        out->num = args[0].num;
        if (snum_round(&out->num, (int8_t)places) != ERR_OK)
            set_err(out, ERR_NUM);
        return;
    }

    case FN_MOD: {
        err_t e;
        if (!as_number(&args[0])) { *out = args[0]; return; }
        if (!as_number(&args[1])) { *out = args[1]; return; }
        out->num = args[0].num;
        e = snum_mod(&out->num, &args[1].num);
        if (e != ERR_OK)
            set_err(out, e);
        return;
    }

    case FN_LEN: {
        uint16_t n = 0;
        if (args[0].kind == FV_STR)
            n = strpool_len(args[0].u.sid);
        else if (args[0].kind == FV_NUM || args[0].kind == FV_BOOL) {
            char buf[SNUM_TEXT_MAX];
            n = snum_to_text(&args[0].num, buf);
        }
        snum_from_i16(&out->num, (int16_t)n);
        return;
    }

    default:
        set_err(out, ERR_NAME);
        return;
    }
}

/* --- the machine ----------------------------------------------------- */

/* A range used where a single value is wanted collapses to its first cell,
 * which is the least surprising of the bad options. */
static void deref(fval_t *v)
{
    if (v->kind == FV_RANGE)
        load_cell_on(v->err, v->u.rng.r1, v->u.rng.c1, v);
}

/* Opcodes are read straight out of banked RAM rather than copied to a local
 * array: the array would be 320 bytes and cc65 will not put that on the
 * stack. A byte fetch is a bank register write and a load, so a fifty-byte
 * formula costs a few hundred cycles per evaluation — invisible next to the
 * floating point it is driving. */
#define CB(fr, i) bank_peek((fr), (uint16_t)(FR_CODE + (i)))

/* The evaluation stack. Static, not a local: 130 bytes of a 512-byte C
 * stack overflowed it on the deepest path.
 *
 * SAFE ONLY BECAUSE run() IS NOT RE-ENTRANT. It calls call_fn, which calls
 * agg_value and load_cell, and none of those call back -- a formula naming
 * another formula reads that one's cached value. If run() ever becomes
 * re-entrant this must go back on the stack, and the stack must grow.
 *
 * Golden RAM, so it costs no resident image. */
X16S_GOLDEN_BEGIN
static fval_t eval_stack[STACK_MAX];
X16S_GOLDEN_END

static void run(handle_t fr, fval_t *result)
{
#define stack eval_stack
    uint8_t  sp = 0;
    uint16_t pc = 0, len = bank_peek16(fr, FR_CODELEN);

    /* Not left over from whatever ran last, and not zero either -- zero is
     * sheet 0, which would silently redirect every plain reference in the
     * first formula after a cross-sheet one. */
    ref_sheet = SH_HERE;

    while (pc < len) {
        uint8_t op = CB(fr, pc); ++pc;

        /* THE STACK POINTER IS NEVER ALLOWED OUT OF RANGE.
         *
         * Checked once here rather than in each popping case, so a case
         * added later cannot forget it. Without this a bad case writes
         * kilobytes past a 130-byte array -- through golden RAM and the
         * program itself. With it, the same fault is a #SYNTAX! cell. */
        if (sp > STACK_MAX) {
            set_err(result, ERR_SYNTAX);
            return;
        }

        /* Every push needs room; every operator needs operands. Checking
         * once here keeps the individual cases free of it. */
        if (op <= OP_PUSH_RANGE) {
            if (sp >= STACK_MAX) {
                set_err(result, ERR_LIMIT);
                return;
            }
            /* Cleared once here rather than in each push case. OP_END is
             * in this range too and does not push, but clearing a slot
             * nothing goes on to read is free. */
            memset(&stack[sp], 0, sizeof stack[sp]);
        }

        switch (op) {
        case OP_END:
            goto done;

        case OP_PUSH_NUM:
            stack[sp].kind = FV_NUM;
            {
                uint8_t i;
                for (i = 0; i < 5; ++i)
                    stack[sp].num.b[i] = CB(fr, pc + i);
            }
            pc += 5;
            ++sp;
            break;

        case OP_PUSH_STR:
            stack[sp].kind = FV_STR;
            stack[sp].u.sid = (uint16_t)(CB(fr, pc)
                                       | ((uint16_t)CB(fr, pc + 1) << 8));
            pc += 2;
            ++sp;
            break;

        case OP_PUSH_BOOL:
            stack[sp].kind = FV_BOOL;
            snum_from_i16(&stack[sp].num, CB(fr, pc) ? 1 : 0);
            ++pc;
            ++sp;
            break;

        case OP_SHEET:
            /* A prefix, not a push: it says which sheet the next reference
             * reads from and is cleared by that reference. */
            ref_sheet = CB(fr, pc);
            ++pc;
            break;

        case OP_PUSH_CELL:
            load_cell_on(ref_sheet,
                         (uint16_t)(CB(fr, pc)
                                    | ((uint16_t)CB(fr, pc + 1) << 8)),
                         CB(fr, pc + 2), &stack[sp]);
            ref_sheet = SH_HERE;
            pc += 3;
            ++sp;
            break;

        case OP_PUSH_RANGE:
            stack[sp].kind = FV_RANGE;
            /* The sheet rides in `err`, which a range has no other use for
             * -- err means something only when kind is FV_ERR. That keeps
             * fval_t at thirteen bytes; widening it would have cost ten
             * more on the C stack, which has 127 to spare. */
            stack[sp].err = ref_sheet;
            ref_sheet = SH_HERE;
            stack[sp].u.rng.r1 = (uint16_t)(CB(fr, pc)
                                          | ((uint16_t)CB(fr, pc + 1) << 8));
            stack[sp].u.rng.c1 = CB(fr, pc + 2);
            stack[sp].u.rng.r2 = (uint16_t)(CB(fr, pc + 3)
                                          | ((uint16_t)CB(fr, pc + 4) << 8));
            stack[sp].u.rng.c2 = CB(fr, pc + 5);
            pc += 6;
            ++sp;
            break;

        case OP_NEG:
            if (sp < 1) { set_err(result, ERR_SYNTAX); return; }
            deref(&stack[sp-1]);
            if (as_number(&stack[sp-1]))
                snum_neg(&stack[sp-1].num);
            break;

        case OP_CALL: {
            /* cc65 is C89: every declaration in a block comes before the
             * first statement. */
            uint8_t fn, argc;
            fval_t r;

            fn = CB(fr, pc);
            ++pc;
            argc = CB(fr, pc);
            ++pc;
            if (sp < argc) { set_err(result, ERR_SYNTAX); return; }
            call_fn(fn, &stack[sp - argc], argc, &r);
            sp = (uint8_t)(sp - argc);
            stack[sp++] = r;
            break;
        }

        default: {
            /* Every remaining opcode is a binary operator. */
            fval_t *a, *b;
            err_t e = ERR_OK;

            if (sp < 2) { set_err(result, ERR_SYNTAX); return; }
            a = &stack[sp-2];
            b = &stack[sp-1];
            deref(a);
            deref(b);

            if (a->kind == FV_ERR) { stack[sp-2] = *a; --sp; break; }
            if (b->kind == FV_ERR) { stack[sp-2] = *b; --sp; break; }

            if (op >= OP_EQ && op <= OP_GE) {
                int8_t cmp;
                uint8_t truth;
                /* Text compares with text, numbers with numbers; a mixture
                 * is not an error, it just never compares equal. */
                if (a->kind == FV_STR || b->kind == FV_STR) {
                    uint8_t same = (a->kind == FV_STR && b->kind == FV_STR
                                    && a->u.sid == b->u.sid);
                    cmp = same ? 0 : 1;
                } else {
                    cmp = snum_cmp(&a->num, &b->num);
                }
                switch (op) {
                case OP_EQ: truth = (cmp == 0); break;
                case OP_NE: truth = (cmp != 0); break;
                case OP_LT: truth = (cmp < 0);  break;
                case OP_GT: truth = (cmp > 0);  break;
                case OP_LE: truth = (cmp <= 0); break;
                default:    truth = (cmp >= 0); break;
                }
                memset(a, 0, sizeof *a);
                a->kind = FV_BOOL;
                snum_from_i16(&a->num, truth ? 1 : 0);
                --sp;
                break;
            }

            if (!as_number(a)) { --sp; break; }
            if (!as_number(b)) { stack[sp-2] = *b; --sp; break; }

            switch (op) {
            case OP_ADD: e = snum_add(&a->num, &b->num); break;
            case OP_SUB: e = snum_sub(&a->num, &b->num); break;
            case OP_MUL: e = snum_mul(&a->num, &b->num); break;
            case OP_DIV: e = snum_div(&a->num, &b->num); break;
            case OP_POW: e = snum_pow(&a->num, &b->num); break;
            default:     e = ERR_SYNTAX; break;
            }
            a->kind = FV_NUM;
            if (e != ERR_OK)
                set_err(a, e);
            --sp;
            break;
        }
        }
    }

done:
    if (sp != 1) {
        set_err(result, ERR_SYNTAX);
        return;
    }
    deref(&stack[0]);
    *result = stack[0];
}
#undef stack

/* --- scheduling ------------------------------------------------------ */

static void store_result(handle_t fr, const fval_t *v)
{
    uint8_t kind;

    switch (v->kind) {
    case FV_ERR:  kind = CELL_ERROR;   break;
    case FV_STR:  kind = CELL_TEXT;    break;
    case FV_BOOL: kind = CELL_BOOLEAN; break;
    default:      kind = CELL_NUMBER;  break;
    }

    /* One five-byte field however it is filled. Three branches each with
     * their own buffer, memset and bank_write came to noticeably more code
     * than choosing what goes in one. */
    {
        uint8_t z[5];

        memset(z, 0, sizeof z);
        if (kind == CELL_ERROR) {
            z[0] = v->err ? v->err : (uint8_t)ERR_VALUE;
        } else if (kind == CELL_TEXT) {
            z[0] = (uint8_t)v->u.sid;
            z[1] = (uint8_t)(v->u.sid >> 8);
        } else {
            memcpy(z, v->num.b, 5);
        }
        bank_poke(fr, FR_KIND, kind);
        bank_write(fr, FR_VALUE, z, 5);
    }
    bank_poke(fr, FR_STATUS, FS_CLEAN);
}

/* Walk every formula cell, calling `visit`. Returns how many there are. */
static uint16_t each_formula(void (*visit)(handle_t fr), uint8_t dirty_only,
                             uint16_t *did)
{
    const cellstore_t *cs;
    uint16_t row, total = 0;
    uint8_t  home = wb_sheet_i, s;

    if (did)
        *did = 0;

    /* EVERY sheet, not just the active one. A formula on Sheet1 reading
     * Sheet2!A1 depends on a formula the old single-sheet walk never
     * visited, so that dependency stayed dirty for ever and the fixed
     * point declared it a circular reference. Passing over the whole
     * workbook lets the same algorithm settle it: no ordering, no
     * dependency graph, just more cells in the sweep. */
    for (s = 0; s < wb_sheet_n; ++s) {
        uint16_t here = 0;

        /* Skip a sheet the last full walk found no formulas on. There is
         * no index of where formulas are, so they are found by reading
         * every cell of every row, and a sheet of raw data is most of that
         * work for nothing.
         *
         * Cannot go stale within a recalculation: the note is written only
         * by a full walk and read only by a partial one, and
         * formula_recalc always begins with a full walk -- so the pass
         * that builds it and the passes that use it are one operation. */
        if (dirty_only && !sheet_has_formulas[s])
            continue;

        wb_sheet_switch(s);
        cs = wb_cells();
        for (row = cells_next_row(cs, 0); row < X16S_MAX_ROWS; ) {
            uint16_t n = cells_row_count(cs, row), i;
            for (i = 0; i < n; ++i) {
                cell_record_t rec;
                handle_t fr;
                if (!cells_row_at(cs, row, i, &rec)
                    || rec.type != CELL_FORMULA)
                    continue;
                fr = formula_rec(&rec);
                ++total;
                ++here;
                if (dirty_only && bank_peek(fr, FR_STATUS) != FS_DIRTY)
                    continue;
                visit(fr);
                if (did)
                    ++(*did);
            }
            if (row == X16S_MAX_ROWS - 1)
                break;
            row = cells_next_row(cs, (uint16_t)(row + 1));
        }

        if (!dirty_only)
            sheet_has_formulas[s] = (uint8_t)(here != 0);
    }

    /* Back where the caller left it: the grid is showing this one. */
    wb_sheet_switch(home);
    return total;
}

static void mark_dirty(handle_t fr)
{
    bank_poke(fr, FR_STATUS, FS_DIRTY);
}

void formula_touch_all(void)
{
    each_formula(mark_dirty, 0, 0);
}

/* Set during a pass so the visitor can report back without a return value. */
X16S_GOLDEN_BEGIN
static uint16_t pass_done;
X16S_GOLDEN_END

static void eval_one(handle_t fr)
{
    fval_t v;

    memset(&v, 0, sizeof v);
    pending = 0;
    run(fr, &v);

    /* If anything it read was still dirty, the answer is provisional —
     * throw it away and try again next pass, when its inputs are ready. */
    if (pending)
        return;

    store_result(fr, &v);
    ++pass_done;
}

static void cycle_one(handle_t fr)
{
    uint8_t z[5];

    memset(z, 0, sizeof z);
    z[0] = (uint8_t)ERR_CYCLE;
    bank_poke(fr, FR_KIND, CELL_ERROR);
    bank_write(fr, FR_VALUE, z, 5);
    bank_poke(fr, FR_STATUS, FS_CLEAN);
}

err_t formula_recalc(void)
{
    uint16_t total, remaining, guard;

    total = each_formula(mark_dirty, 0, 0);
    if (total == 0)
        return ERR_OK;

    /* Each pass resolves at least one more level of the dependency chain,
     * so `total` passes is a hard upper bound on a sheet with no cycles. */
    for (guard = 0; guard <= total; ++guard) {
        pass_done = 0;
        each_formula(eval_one, 1, &remaining);
        if (remaining == 0)
            return ERR_OK;              /* nothing left dirty */
        if (pass_done == 0)
            break;                      /* no progress: only cycles remain */
    }

    /* Whatever is still dirty is part of a cycle, or depends on one. */
    each_formula(cycle_one, 1, 0);
    return ERR_OK;
}

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
