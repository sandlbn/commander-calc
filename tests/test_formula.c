#include "test.h"
#include "../src/platform/bankmem.h"
#include "../src/platform/banked_ram.h"
#include "../src/workbook/workbook.h"
#include "../src/formula/formula.h"

static void setup(void)
{
    CHECK(bankmem_host_init(64));
    CHECK_EQ(bank_heap_init(1, 0), ERR_OK);
    CHECK_EQ(wb_init(), ERR_OK);
}

static void teardown(void) { bankmem_host_free(); }

/* Put a formula in A1 and report what it displays. */
static void check_expr(const char *file, int line,
                       const char *expr, const char *want)
{
    char buf[WB_TEXT_MAX];
    char text[128];
    err_t e;

    text[0] = '=';
    strncpy(text + 1, expr, sizeof text - 2);
    text[sizeof text - 1] = '\0';

    e = wb_set_text(0, 0, text);
    ++tests_checked;
    if (e != ERR_OK) {
        char d[160];
        snprintf(d, sizeof d, "=%s failed to compile: %s", expr,
                 err_message(e));
        test_report_fail(file, line, "compile", d);
        return;
    }

    wb_display_text(0, 0, buf, sizeof buf);
    ++tests_checked;
    if (strcmp(buf, want) != 0) {
        char d[200];
        snprintf(d, sizeof d, "=%s gave \"%s\", want \"%s\"", expr, buf, want);
        test_report_fail(file, line, "evaluate", d);
    }
}
#define EXPR(e, w) check_expr(__FILE__, __LINE__, (e), (w))

/* A formula that must be rejected at compile time. */
static void check_bad(const char *file, int line, const char *expr, err_t want)
{
    char text[128];
    err_t e;

    text[0] = '=';
    strncpy(text + 1, expr, sizeof text - 2);
    text[sizeof text - 1] = '\0';

    e = wb_set_text(5, 5, text);
    ++tests_checked;
    if (e != want) {
        char d[200];
        snprintf(d, sizeof d, "=%s gave %s, want %s", expr,
                 err_message(e), err_message(want));
        test_report_fail(file, line, "reject", d);
    }
}
#define BAD(e, w) check_bad(__FILE__, __LINE__, (e), (w))

/* --- arithmetic ----------------------------------------------------- */

static void test_arithmetic(void)
{
    setup();

    EXPR("1+2", "3");
    EXPR("10-4", "6");
    EXPR("6*7", "42");
    EXPR("10/4", "2.5");
    EXPR("2^10", "1024");
    EXPR("-5", "-5");
    EXPR("-(3+4)", "-7");
    EXPR("8.5+1.5", "10");

    /* Precedence, and that parentheses override it. */
    EXPR("2+3*4", "14");
    EXPR("(2+3)*4", "20");
    EXPR("100/10/2", "5");            /* left to right, not 100/(10/2) */
    EXPR("10-3-2", "5");
    EXPR("2^3^2", "64");              /* left to right, as Excel does */
    EXPR("-2^2", "4");                /* unary minus binds first */

    /* Whitespace is not significant. */
    EXPR("  1  +  2  ", "3");

    teardown();
}

static void test_comparisons(void)
{
    setup();

    EXPR("1<2", "TRUE");
    EXPR("2<1", "FALSE");
    EXPR("2=2", "TRUE");
    EXPR("2<>2", "FALSE");
    EXPR("2>=2", "TRUE");
    EXPR("2<=1", "FALSE");
    EXPR("1+1=2", "TRUE");            /* arithmetic binds tighter */

    teardown();
}

/* --- references ------------------------------------------------------ */

static void test_references(void)
{
    setup();

    wb_set_text(0, 1, "10");          /* B1 */
    wb_set_text(1, 1, "20");          /* B2 */
    wb_set_text(2, 1, "30");          /* B3 */

    EXPR("B1", "10");
    EXPR("B1+B2", "30");
    EXPR("B1*2", "20");

    /* Absolute markers parse and mean the same thing today. */
    EXPR("$B$1", "10");
    EXPR("$B1+B$2", "30");

    /* An empty cell reads as zero, not as an error. */
    EXPR("Z90+1", "1");

    /* Case does not matter. */
    EXPR("b1", "10");

    teardown();
}

static void test_ranges(void)
{
    setup();

    wb_set_text(0, 1, "10");
    wb_set_text(1, 1, "20");
    wb_set_text(2, 1, "30");
    wb_set_text(0, 2, "1");
    wb_set_text(1, 2, "2");

    EXPR("SUM(B1:B3)", "60");
    EXPR("SUM(B1:C2)", "33");         /* a rectangle, not a row */
    EXPR("SUM(B3:B1)", "60");         /* reversed corners normalise */
    EXPR("SUM(B1:B3)+1", "61");
    EXPR("SUM(B1:B3,C1:C2)", "63");   /* several range arguments */
    EXPR("SUM(B1:B3,100)", "160");    /* ranges and scalars mixed */

    /* Blanks inside a range are skipped rather than counted as zero — the
     * difference shows up in AVERAGE and COUNT, not in SUM. */
    EXPR("COUNT(B1:B10)", "3");
    EXPR("AVERAGE(B1:B3)", "20");
    EXPR("MIN(B1:B3)", "10");
    EXPR("MAX(B1:B3)", "30");

    teardown();
}

/* --- functions -------------------------------------------------------- */

static void test_functions(void)
{
    setup();

    EXPR("SUM(1,2,3)", "6");
    EXPR("AVERAGE(2,4)", "3");
    EXPR("MIN(5,2,8)", "2");
    EXPR("MAX(5,2,8)", "8");
    EXPR("COUNT(1,2,3)", "3");

    EXPR("IF(1>0,10,20)", "10");
    EXPR("IF(1<0,10,20)", "20");
    EXPR("IF(1>0,\"yes\",\"no\")", "yes");

    EXPR("AND(1,1)", "TRUE");
    EXPR("AND(1,0)", "FALSE");
    EXPR("OR(0,1)", "TRUE");
    EXPR("OR(0,0)", "FALSE");
    EXPR("NOT(0)", "TRUE");
    EXPR("NOT(1)", "FALSE");

    EXPR("ABS(-7)", "7");
    EXPR("ABS(7)", "7");
    EXPR("INT(2.7)", "2");
    EXPR("INT(-2.7)", "-3");          /* floor, as BASIC INT */
    EXPR("ROUND(3.14159,2)", "3.14");
    EXPR("ROUND(2.5,0)", "3");        /* half away from zero */
    EXPR("ROUND(2.4)", "2");          /* places defaults to 0 */
    EXPR("MOD(7,3)", "1");
    EXPR("MOD(-1,3)", "2");           /* result takes the divisor's sign */
    EXPR("LEN(\"hello\")", "5");
    EXPR("LEN(\"\")", "0");

    /* Nesting, which is what makes any of this useful. */
    EXPR("IF(SUM(1,2)>2,ABS(-1),0)", "1");
    EXPR("ROUND(AVERAGE(1,2),0)", "2");

    /* Case-insensitive names. */
    EXPR("sum(1,2)", "3");
    EXPR("Sum(1,2)", "3");

    teardown();
}

/* --- errors ----------------------------------------------------------- */

static void test_errors(void)
{
    setup();

    EXPR("1/0", "#DIV/0!");
    EXPR("SUM(1,2)/0", "#DIV/0!");
    /* An error propagates rather than being swallowed. */
    EXPR("1/0+1", "#DIV/0!");
    EXPR("SUM(A5:A6)", "0");

    wb_set_text(9, 0, "text");        /* A10 */
    EXPR("A10+1", "#VALUE!");
    EXPR("SUM(A10:A10)", "0");        /* text in a range is skipped */

    /* Rejected at compile time, so nothing is stored. */
    BAD("1+", ERR_SYNTAX);
    BAD("(1", ERR_SYNTAX);
    BAD("1 2", ERR_SYNTAX);
    BAD("NOSUCH(1)", ERR_NAME);
    BAD("SUM", ERR_NAME);             /* a function needs its parentheses */
    BAD("IF(1)", ERR_VALUE);          /* too few arguments */
    BAD("ABS(1,2)", ERR_VALUE);       /* too many */
    BAD("A0", ERR_REF);               /* rows are 1-based */
    BAD("ZZ1", ERR_REF);              /* past the last column */
    BAD("A70000", ERR_REF);

    teardown();
}

/* --- recalculation ---------------------------------------------------- */

static void test_recalc(void)
{
    char buf[WB_TEXT_MAX];

    setup();

    wb_set_text(0, 0, "10");                    /* A1 */
    wb_set_text(1, 0, "=A1*2");                 /* A2 */
    wb_display_text(1, 0, buf, sizeof buf);
    CHECK_STR(buf, "20");

    /* Changing an input updates the formula. */
    wb_set_text(0, 0, "50");
    wb_display_text(1, 0, buf, sizeof buf);
    CHECK_STR(buf, "100");

    /* Deleting an input makes it zero, not an error. */
    wb_clear(0, 0);
    wb_display_text(1, 0, buf, sizeof buf);
    CHECK_STR(buf, "0");

    teardown();
}

/* --- manual recalculation ---------------------------------------------
 *
 * Without dependency tracking every edit recalculates every formula, which
 * is seconds on a sheet with a few hundred of them. The switch defers that
 * work; what it must NOT do is lose it. So: with it on the answer goes
 * stale and wb_calc_due says so, and asking for a recalculation brings it
 * back. Turning the switch off again is the grid's job -- it has to load
 * an overlay to do the sum -- so this checks the workbook's half.
 */
static void test_manual_recalc(void)
{
    char buf[WB_TEXT_MAX];

    setup();

    wb_set_text(0, 0, "10");                    /* A1 */
    wb_set_text(1, 0, "=A1*2");                 /* A2 */
    wb_display_text(1, 0, buf, sizeof buf);
    CHECK_STR(buf, "20");
    CHECK_EQ(wb_calc_due, 0);

    wb_manual = 1;
    wb_set_text(0, 0, "50");

    /* The edit landed; the answer that depends on it did not move. */
    wb_display_text(0, 0, buf, sizeof buf);
    CHECK_STR(buf, "50");
    wb_display_text(1, 0, buf, sizeof buf);
    CHECK_STR(buf, "20");

    /* And the program knows it is showing something stale, which is what
     * puts RECALC in the status bar. */
    CHECK_EQ(wb_calc_due, 1);

    /* Asking for it catches up, and clears the flag. */
    formula_recalc();
    wb_calc_due = 0;
    wb_display_text(1, 0, buf, sizeof buf);
    CHECK_STR(buf, "100");

    /* Back to automatic: the next edit recalculates on its own again. */
    wb_manual = 0;
    wb_set_text(0, 0, "1");
    wb_display_text(1, 0, buf, sizeof buf);
    CHECK_STR(buf, "2");
    CHECK_EQ(wb_calc_due, 0);

    teardown();
}

/* Order of entry must not matter: a formula written before the formula it
 * depends on still resolves. This is what the multi-pass scheduler is for. */
static void test_forward_references(void)
{
    char buf[WB_TEXT_MAX];

    setup();

    wb_set_text(0, 0, "=A2+1");       /* refers forward */
    wb_set_text(1, 0, "=A3+1");       /* and again */
    wb_set_text(2, 0, "10");

    wb_display_text(0, 0, buf, sizeof buf); CHECK_STR(buf, "12");
    wb_display_text(1, 0, buf, sizeof buf); CHECK_STR(buf, "11");

    teardown();
}

static void test_chain(void)
{
    char buf[WB_TEXT_MAX];
    uint8_t i;

    setup();
    wb_set_text(0, 0, "1");
    /* A deep chain exercises the pass count: each pass resolves one link. */
    for (i = 1; i < 10; ++i) {
        char f[16];
        snprintf(f, sizeof f, "=A%u+1", i);
        wb_set_text(i, 0, f);
    }
    wb_display_text(9, 0, buf, sizeof buf);
    CHECK_STR(buf, "10");
    teardown();
}

static void test_cycles(void)
{
    char buf[WB_TEXT_MAX];

    setup();

    /* Direct self-reference. */
    wb_set_text(0, 0, "=A1+1");
    wb_display_text(0, 0, buf, sizeof buf);
    CHECK_STR(buf, "#CYCLE!");

    teardown();
    setup();

    /* Indirect, through two cells. */
    wb_set_text(0, 0, "=A2");
    wb_set_text(1, 0, "=A1");
    wb_display_text(0, 0, buf, sizeof buf); CHECK_STR(buf, "#CYCLE!");
    wb_display_text(1, 0, buf, sizeof buf); CHECK_STR(buf, "#CYCLE!");

    teardown();
    setup();

    /* A cycle must not poison formulas that are merely nearby. */
    wb_set_text(0, 0, "=A2");
    wb_set_text(1, 0, "=A1");
    wb_set_text(4, 0, "7");
    wb_set_text(5, 0, "=A5*2");
    wb_display_text(5, 0, buf, sizeof buf); CHECK_STR(buf, "14");

    teardown();
}

/* --- the formula bar and storage -------------------------------------- */

static void test_source_is_kept(void)
{
    char buf[WB_TEXT_MAX];
    cell_record_t rec;

    setup();
    wb_set_text(0, 1, "5");
    wb_set_text(0, 0, "=B1*2");

    CHECK_EQ(wb_get(0, 0, &rec), 1);
    CHECK_EQ(rec.type, CELL_FORMULA);

    /* The grid shows the answer... */
    wb_display_text(0, 0, buf, sizeof buf);
    CHECK_STR(buf, "10");
    /* ...and the formula bar shows what was typed. */
    wb_edit_text(0, 0, buf, sizeof buf);
    CHECK_STR(buf, "=B1*2");

    /* A numeric result aligns right, like any other number. */
    CHECK_EQ(wb_align_right(&rec), 1);

    teardown();
}

/* The acceptance workbook from the plan, exactly as specified. */
static void test_acceptance_workbook(void)
{
    char buf[WB_TEXT_MAX];

    setup();

    wb_set_text(0, 0, "10");            /* A1 */
    wb_set_text(1, 0, "20");            /* A2 */
    wb_set_text(2, 0, "30");            /* A3 */
    wb_set_text(3, 0, "=SUM(A1:A3)");   /* A4 */
    wb_set_text(0, 1, "=A4/3");         /* B1 */
    wb_set_text(1, 1, "=IF(B1>15,1,0)");/* B2 */

    wb_display_text(3, 0, buf, sizeof buf); CHECK_STR(buf, "60");
    wb_display_text(0, 1, buf, sizeof buf); CHECK_STR(buf, "20");
    wb_display_text(1, 1, buf, sizeof buf); CHECK_STR(buf, "1");

    /* And it stays correct when an input changes. */
    wb_set_text(0, 0, "1");
    wb_display_text(3, 0, buf, sizeof buf); CHECK_STR(buf, "51");
    wb_display_text(0, 1, buf, sizeof buf); CHECK_STR(buf, "17");
    wb_display_text(1, 1, buf, sizeof buf); CHECK_STR(buf, "1");

    wb_set_text(1, 0, "0");
    wb_set_text(2, 0, "0");
    wb_display_text(3, 0, buf, sizeof buf); CHECK_STR(buf, "1");
    wb_display_text(0, 1, buf, sizeof buf); CHECK_STR(buf, ".333333333");
    wb_display_text(1, 1, buf, sizeof buf); CHECK_STR(buf, "0");

    teardown();
}

void test_formula(void)
{
    test_arithmetic();
    test_comparisons();
    test_references();
    test_ranges();
    test_functions();
    test_errors();
    test_recalc();
    test_manual_recalc();
    test_forward_references();
    test_chain();
    test_cycles();
    test_source_is_kept();
    test_acceptance_workbook();
}
