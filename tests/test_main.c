/* test_main.c — the host test runner.
 *
 * Every portable module gets a test_<module>() entry in the table below.
 * Add new suites here; there is no registration magic.
 */
#include "test.h"

int  tests_checked;
int  tests_failed;
const char *tests_current = "";

void test_report_fail(const char *file, int line, const char *expr,
                      const char *detail)
{
    ++tests_failed;
    fprintf(stderr, "FAIL %s\n  %s:%d: %s\n", tests_current, file, line, expr);
    if (detail)
        fprintf(stderr, "  %s\n", detail);
}

/* --- suites ------------------------------------------------------- */

void test_errors(void);
void test_overlay(void);
void test_crc32(void);
void test_bank(void);
void test_number(void);
void test_cells(void);
void test_workbook(void);
void test_grid(void);
void test_x16s(void);
void test_formula(void);
void test_csv(void);
void test_zip(void);
void test_inflate(void);
void test_xml(void);
void test_xlsx(void);
void test_xlsx_sheet(void);
void test_xlsx_import(void);
void test_xlsx_export(void);
void test_menu(void);
void test_sheets(void);
void test_strings(void);
void test_filedlg(void);

static const struct {
    const char *name;
    void (*run)(void);
} suites[] = {
    { "errors",  test_errors  },
    { "overlay", test_overlay },
    { "crc32",   test_crc32   },
    { "bank",    test_bank    },
    { "number",  test_number  },
    { "cells",    test_cells    },
    { "workbook", test_workbook },
    { "grid",     test_grid     },
    { "x16s",     test_x16s     },
    { "formula",  test_formula  },
    { "csv",      test_csv      },
    { "zip",      test_zip      },
    { "inflate",  test_inflate  },
    { "xml",      test_xml      },
    { "xlsx",     test_xlsx     },
    { "sheet",    test_xlsx_sheet },
    { "ximport",  test_xlsx_import },
    { "xexport",  test_xlsx_export },
    { "menu",     test_menu     },
    { "sheets",   test_sheets   },
    { "strings",  test_strings  },
    { "filedlg",  test_filedlg  },
};

int main(void)
{
    unsigned i;
    int before;

    for (i = 0; i < sizeof suites / sizeof suites[0]; ++i) {
        tests_current = suites[i].name;
        before = tests_failed;
        suites[i].run();
        printf("%-12s %s\n", suites[i].name,
               tests_failed == before ? "ok" : "FAILED");
    }

    printf("\n%d checks, %d failed\n", tests_checked, tests_failed);
    return tests_failed != 0;
}
