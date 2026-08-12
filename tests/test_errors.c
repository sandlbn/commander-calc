#include "test.h"
#include "../src/util/errors.h"

void test_errors(void)
{
    /* Every code has text, including ones nobody thought about. */
    CHECK(err_message(ERR_OK) != 0);
    CHECK(err_message(ERR_CYCLE) != 0);
    CHECK_STR(err_message((err_t)999), "Unknown error");

    /* The six formula errors have their proper names. */
    CHECK_STR(err_cell_text(ERR_NAME), "#NAME?");
    CHECK_STR(err_cell_text(ERR_DIVZERO), "#DIV/0!");
    CHECK_STR(err_cell_text(ERR_CYCLE), "#CYCLE!");

    /* Anything else is not a formula error at all -- it is this program
     * having gone wrong somewhere -- so it shows its number. It used to
     * return null and the renderer printed a bare "#ERR!", which said only
     * that something had happened and nothing about what. */
    CHECK_STR(err_cell_text(ERR_NOMEM), "#ERR10!");     /* out of banked RAM */
    CHECK_STR(err_cell_text(ERR_OK), "#ERR0!");
    CHECK_STR(err_cell_text((err_t)255), "#ERR255!");

    /* It never returns null: both callers strcpy() the answer straight out
     * and neither tests it. */
    CHECK(err_cell_text(ERR_IO) != 0);
}
