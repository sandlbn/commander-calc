/* test.h — the whole test framework. No dependencies, deliberately.
 *
 * Usage:
 *     #include "test.h"
 *     void test_widget(void) {
 *         CHECK(widget_init() == ERR_OK);
 *         CHECK_EQ(widget_count(), 3);
 *     }
 * then add test_widget() to the table in test_main.c.
 */
#ifndef X16S_TEST_H
#define X16S_TEST_H

#include <stdio.h>
#include <string.h>
#include "../src/x16sheet.h"

extern int  tests_checked;
extern int  tests_failed;
extern const char *tests_current;

void test_report_fail(const char *file, int line, const char *expr,
                      const char *detail);

#define CHECK(expr)                                                        \
    do {                                                                   \
        ++tests_checked;                                                   \
        if (!(expr))                                                       \
            test_report_fail(__FILE__, __LINE__, #expr, 0);                \
    } while (0)

/* Integer comparison that prints both sides on failure. */
#define CHECK_EQ(got, want)                                                \
    do {                                                                   \
        long g_ = (long)(got), w_ = (long)(want);                          \
        ++tests_checked;                                                   \
        if (g_ != w_) {                                                    \
            char buf_[80];                                                 \
            snprintf(buf_, sizeof buf_, "got %ld, want %ld", g_, w_);      \
            test_report_fail(__FILE__, __LINE__, #got " == " #want, buf_); \
        }                                                                  \
    } while (0)

#define CHECK_STR(got, want)                                               \
    do {                                                                   \
        const char *g_ = (got), *w_ = (want);                              \
        ++tests_checked;                                                   \
        if (!g_ || !w_ || strcmp(g_, w_) != 0) {                           \
            char buf_[160];                                                \
            /* Bounded per string: two unbounded %s into a fixed buffer   \
             * is a truncation warning at every use of the macro, and a   \
             * failure message is no place to lose a build's worth of     \
             * diagnostics. 64 each fits with room for the wrapper. */    \
            snprintf(buf_, sizeof buf_, "got \"%.64s\", want \"%.64s\"",   \
                     g_ ? g_ : "(null)", w_ ? w_ : "(null)");             \
            test_report_fail(__FILE__, __LINE__, #got " == " #want, buf_); \
        }                                                                  \
    } while (0)

#endif /* X16S_TEST_H */
