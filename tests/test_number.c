#include "test.h"
#include "../src/util/number.h"
#include "../src/util/number_prim.h"

/* Compare a value against a decimal literal by rendering both. Nine
 * significant digits is the honest precision of the format; comparing bytes
 * would fail on the X16 for reasons that are not bugs. */
static void check_text(const char *file, int line,
                       const snum_t *v, const char *want)
{
    char got[SNUM_TEXT_MAX];

    snum_to_text(v, got);
    ++tests_checked;
    if (strcmp(got, want) != 0) {
        char detail[160];
        snprintf(detail, sizeof detail, "got \"%s\", want \"%s\"", got, want);
        test_report_fail(file, line, "snum_to_text", detail);
    }
}
#define CHECK_NUM(v, want) check_text(__FILE__, __LINE__, (v), (want))

static void check_parse(const char *file, int line,
                        const char *in, const char *want)
{
    snum_t v;
    err_t e = snum_parse(in, &v);

    ++tests_checked;
    if (e != ERR_OK) {
        char detail[160];
        snprintf(detail, sizeof detail, "parse(\"%s\") failed: %s",
                 in, err_message(e));
        test_report_fail(file, line, "snum_parse", detail);
        return;
    }
    check_text(file, line, &v, want);
}
#define CHECK_PARSE(in, want) check_parse(__FILE__, __LINE__, (in), (want))

/* --- the format itself -------------------------------------------- */

static void test_encoding(void)
{
    /* The three literals documented in x16-docs chapter 06. Getting these
     * wrong would make every constant in the program subtly incorrect. */
    static const snum_t k981 = { { 0x84, 0x1C, 0xF5, 0xC2, 0x8F } };
    static const snum_t k5   = { { 0x83, 0x20, 0x00, 0x00, 0x00 } };
    static const snum_t k2   = { { 0x82, 0x00, 0x00, 0x00, 0x00 } };

    CHECK_NUM(&k981, "9.81");
    CHECK_NUM(&k5, "5");
    CHECK_NUM(&k2, "2");
    CHECK_NUM(&snum_zero, "0");
    CHECK_NUM(&snum_one, "1");
    CHECK_NUM(&snum_ten, "10");

    CHECK(snum_is_zero(&snum_zero));
    CHECK(!snum_is_zero(&snum_one));
}

static void test_sign_and_compare(void)
{
    snum_t a, b, n;

    snum_from_i16(&a, 3);
    snum_from_i16(&b, 7);
    snum_from_i16(&n, -3);

    CHECK_EQ(snum_sign(&a), 1);
    CHECK_EQ(snum_sign(&n), -1);
    CHECK_EQ(snum_sign(&snum_zero), 0);

    CHECK_EQ(snum_cmp(&a, &b), -1);
    CHECK_EQ(snum_cmp(&b, &a), 1);
    CHECK_EQ(snum_cmp(&a, &a), 0);
    CHECK_EQ(snum_cmp(&n, &a), -1);
    CHECK_EQ(snum_cmp(&n, &snum_zero), -1);
}

/* --- arithmetic, including operand order --------------------------- */

static void test_arithmetic(void)
{
    snum_t a, b;

    /* Subtraction and division are the ones that break if the ARG/FAC
     * order is wrong, and they break asymmetrically: 10-4 vs 4-10, and
     * 10/4 vs 0.4. These four checks are the whole reason for the comment
     * block in number_x16.s. */
    snum_from_i16(&a, 10);
    snum_from_i16(&b, 4);
    CHECK_EQ(snum_sub(&a, &b), ERR_OK);
    CHECK_NUM(&a, "6");

    snum_from_i16(&a, 4);
    snum_from_i16(&b, 10);
    CHECK_EQ(snum_sub(&a, &b), ERR_OK);
    CHECK_NUM(&a, "-6");

    snum_from_i16(&a, 10);
    snum_from_i16(&b, 4);
    CHECK_EQ(snum_div(&a, &b), ERR_OK);
    CHECK_NUM(&a, "2.5");

    snum_from_i16(&a, 4);
    snum_from_i16(&b, 10);
    CHECK_EQ(snum_div(&a, &b), ERR_OK);
    CHECK_NUM(&a, ".4");

    snum_from_i16(&a, 6);
    snum_from_i16(&b, 7);
    CHECK_EQ(snum_add(&a, &b), ERR_OK);
    CHECK_NUM(&a, "13");

    snum_from_i16(&a, 6);
    snum_from_i16(&b, 7);
    CHECK_EQ(snum_mul(&a, &b), ERR_OK);
    CHECK_NUM(&a, "42");

    /* The worked example from x16-docs chapter 06: d = g/2 * t^2 */
    CHECK_EQ(snum_parse("9.81", &a), ERR_OK);
    CHECK_EQ(snum_div(&a, &snum_ten), ERR_OK);
    CHECK_EQ(snum_mul(&a, &snum_ten), ERR_OK);
    {
        snum_t two, t;
        snum_from_i16(&two, 2);
        snum_from_i16(&t, 5);
        CHECK_EQ(snum_div(&a, &two), ERR_OK);
        CHECK_EQ(snum_mul(&a, &t), ERR_OK);
        CHECK_EQ(snum_mul(&a, &t), ERR_OK);
        CHECK_NUM(&a, "122.625");
    }
}

static void test_unary(void)
{
    snum_t a;

    snum_from_i16(&a, 7);
    snum_neg(&a);
    CHECK_NUM(&a, "-7");
    snum_abs(&a);
    CHECK_NUM(&a, "7");

    /* Negating zero must stay zero, not become a negative zero. */
    a = snum_zero;
    snum_neg(&a);
    CHECK(snum_is_zero(&a));

    /* INT floors, so it moves negatives away from zero. */
    CHECK_EQ(snum_parse("2.7", &a), ERR_OK);
    snum_int(&a);
    CHECK_NUM(&a, "2");
    CHECK_EQ(snum_parse("-2.7", &a), ERR_OK);
    snum_int(&a);
    CHECK_NUM(&a, "-3");

    CHECK_EQ(snum_parse("2.25", &a), ERR_OK);
    CHECK_EQ(snum_pow(&a, &snum_zero), ERR_OK);
    CHECK_NUM(&a, "1");

    snum_from_i16(&a, 144);
    CHECK_EQ(snum_sqrt(&a), ERR_OK);
    CHECK_NUM(&a, "12");
}

/* --- the guards: none of these may reach the ROM ------------------- */

static void test_guards(void)
{
    snum_t a, b;

    /* Division by zero is the one that would drop the user into BASIC. */
    snum_from_i16(&a, 5);
    CHECK_EQ(snum_div(&a, &snum_zero), ERR_DIVZERO);
    CHECK_NUM(&a, "5");                 /* and must leave the operand alone */

    /* 0/x is fine and is zero. */
    a = snum_zero;
    snum_from_i16(&b, 5);
    CHECK_EQ(snum_div(&a, &b), ERR_OK);
    CHECK(snum_is_zero(&a));

    /* Overflow on multiply: two enormous values. */
    {
        snum_t big = { { 0xFE, 0x00, 0x00, 0x00, 0x00 } };
        a = big;
        CHECK_EQ(snum_mul(&a, &big), ERR_NUM);
    }
    /* But multiplying by zero is always fine, however large the other side. */
    {
        snum_t big = { { 0xFE, 0x00, 0x00, 0x00, 0x00 } };
        a = big;
        CHECK_EQ(snum_mul(&a, &snum_zero), ERR_OK);
        CHECK(snum_is_zero(&a));
    }

    /* Square root of a negative. */
    snum_from_i16(&a, -4);
    CHECK_EQ(snum_sqrt(&a), ERR_NUM);

    /* A negative base with a fractional exponent has no real answer, and
     * the ROM would try exp(y*log(x)) and trap. */
    snum_from_i16(&a, -8);
    CHECK_EQ(snum_parse("0.5", &b), ERR_OK);
    CHECK_EQ(snum_pow(&a, &b), ERR_NUM);

    /* A negative base with an integer exponent is fine. */
    snum_from_i16(&a, -2);
    snum_from_i16(&b, 3);
    CHECK_EQ(snum_pow(&a, &b), ERR_OK);
    CHECK_NUM(&a, "-8");

    /* 0 to a negative power is a division by zero. */
    a = snum_zero;
    snum_from_i16(&b, -1);
    CHECK_EQ(snum_pow(&a, &b), ERR_DIVZERO);

    /* An absurd literal must report, not trap. */
    CHECK_EQ(snum_parse("1E999", &a), ERR_NUM);
}

/* --- parsing ------------------------------------------------------ */

static void test_parse(void)
{
    snum_t v;

    CHECK_PARSE("0", "0");
    CHECK_PARSE("1", "1");
    CHECK_PARSE("42", "42");
    CHECK_PARSE("-42", "-42");
    CHECK_PARSE("+42", "42");
    CHECK_PARSE("123.456", "123.456");
    CHECK_PARSE("-0.5", "-.5");
    CHECK_PARSE("  7  ", "7");
    CHECK_PARSE("1e3", "1000");
    CHECK_PARSE("1.5E2", "150");
    CHECK_PARSE("15e-1", "1.5");
    CHECK_PARSE("8.50", "8.5");
    CHECK_PARSE(".25", ".25");

    /* Rejections. A cell holding "1.2.3" is text, not a broken number, so
     * the caller depends on this failing rather than parsing a prefix. */
    CHECK_EQ(snum_parse("", &v), ERR_SYNTAX);
    CHECK_EQ(snum_parse("abc", &v), ERR_SYNTAX);
    CHECK_EQ(snum_parse("1.2.3", &v), ERR_SYNTAX);
    CHECK_EQ(snum_parse("12x", &v), ERR_SYNTAX);
    CHECK_EQ(snum_parse("-", &v), ERR_SYNTAX);
    CHECK_EQ(snum_parse("1e", &v), ERR_SYNTAX);
}

/* --- integer conversion ------------------------------------------- */

static void test_int_conversion(void)
{
    snum_t v;
    int32_t i32;
    uint16_t u16;

    snum_from_i16(&v, -12345);
    CHECK_EQ(snum_to_i32(&v, &i32), ERR_OK);
    CHECK_EQ(i32, -12345L);

    CHECK_EQ(snum_from_i32(&v, 1000000L), ERR_OK);
    CHECK_NUM(&v, "1000000");
    CHECK_EQ(snum_to_i32(&v, &i32), ERR_OK);
    CHECK_EQ(i32, 1000000L);

    CHECK_EQ(snum_from_i32(&v, -2000000L), ERR_OK);
    CHECK_EQ(snum_to_i32(&v, &i32), ERR_OK);
    CHECK_EQ(i32, -2000000L);

    /* Truncation toward zero, matching the ROM's qint. */
    CHECK_EQ(snum_parse("7.9", &v), ERR_OK);
    CHECK_EQ(snum_to_i32(&v, &i32), ERR_OK);
    CHECK_EQ(i32, 7L);

    snum_from_i16(&v, 300);
    CHECK_EQ(snum_to_u16(&v, &u16), ERR_OK);
    CHECK_EQ(u16, 300);

    snum_from_i16(&v, -1);
    CHECK_EQ(snum_to_u16(&v, &u16), ERR_NUM);

    /* Out of 32-bit range must report rather than return nonsense. */
    CHECK_EQ(snum_parse("1E20", &v), ERR_OK);
    CHECK_EQ(snum_to_i32(&v, &i32), ERR_NUM);
}

/* --- rounding and fixed-place display ------------------------------ */

static void test_round_and_fixed(void)
{
    snum_t v;
    char buf[SNUM_TEXT_MAX];

    CHECK_EQ(snum_parse("2.5", &v), ERR_OK);
    CHECK_EQ(snum_round(&v, 0), ERR_OK);
    CHECK_NUM(&v, "3");                 /* half away from zero, not banker's */

    CHECK_EQ(snum_parse("-2.5", &v), ERR_OK);
    CHECK_EQ(snum_round(&v, 0), ERR_OK);
    CHECK_NUM(&v, "-3");

    CHECK_EQ(snum_parse("3.14159", &v), ERR_OK);
    CHECK_EQ(snum_round(&v, 2), ERR_OK);
    CHECK_NUM(&v, "3.14");

    /* Fixed places: always exactly as many decimals as asked for, which is
     * what a currency column needs. */
    CHECK_EQ(snum_parse("8.5", &v), ERR_OK);
    snum_to_fixed(&v, 2, buf);
    CHECK_STR(buf, "8.50");

    CHECK_EQ(snum_parse("34", &v), ERR_OK);
    snum_to_fixed(&v, 2, buf);
    CHECK_STR(buf, "34.00");

    CHECK_EQ(snum_parse("-1.005", &v), ERR_OK);
    snum_to_fixed(&v, 2, buf);
    CHECK_STR(buf, "-1.01");

    CHECK_EQ(snum_parse("0.5", &v), ERR_OK);
    snum_to_fixed(&v, 0, buf);
    CHECK_STR(buf, "1");

    v = snum_zero;
    snum_to_fixed(&v, 2, buf);
    CHECK_STR(buf, "0.00");

    /* A value below the displayed precision rounds to zero, not to junk. */
    CHECK_EQ(snum_parse("0.001", &v), ERR_OK);
    snum_to_fixed(&v, 2, buf);
    CHECK_STR(buf, "0.00");
}

void test_number(void)
{
    test_encoding();
    test_sign_and_compare();
    test_arithmetic();
    test_unary();
    test_guards();
    test_parse();
    test_int_conversion();
    test_round_and_fixed();
}
