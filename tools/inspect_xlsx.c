/* inspect_xlsx — run any .xlsx through the real import pipeline.
 *
 * Builds the same zip, inflate, xml and xlsx code the X16 runs, against a
 * file given on the command line. The point is to meet workbooks nobody
 * wrote for us: our own fixtures only prove the importer agrees with the
 * fixture generator.
 *
 *     make inspect FILE=/path/to/whatever.xlsx
 *
 * Reports what it found and what it could not handle. A crash or a wrong
 * answer here is a bug that would otherwise have surfaced on the machine,
 * where there is no debugger worth the name.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../src/platform/bankmem.h"
#include "../src/platform/banked_ram.h"
#include "../src/platform/file_io.h"
#include "../src/workbook/workbook.h"
#include "../src/workbook/strings.h"
#include "../src/import/blob.h"
#include "../src/import/zip.h"
#include "../src/import/inflate.h"
#include "../src/import/xlsx.h"
#include "../src/import/xlsx_stage.h"
#include "../src/import/xml.h"

static blob_t staged, expanded;

static err_t fetch(const char *archive, const char *path, blob_t *out)
{
    stage_info_t info;
    err_t e;

    blob_free(out);
    e = xlsx_stage(archive, path, &staged, &info);
    if (e != ERR_OK)
        return e;
    if (!info.compressed) {
        *out = staged;
        memset(&staged, 0, sizeof staged);
        return ERR_OK;
    }
    e = inflate_blob(&staged, out, info.raw_size);
    blob_free(&staged);
    return e;
}

int main(int argc, char **argv)
{
    char dir[512], base[256];
    const char *slash;
    zip_t z;
    zip_entry_t e;
    xlsx_book_t bk;
    static uint16_t ids[X16S_MAX_STRINGS];
    char buf[X16S_MAX_TEXT_LEN];
    err_t err;
    unsigned n = 0, i;

    if (argc < 2) {
        fprintf(stderr, "usage: inspect_xlsx <file.xlsx>\n");
        return 2;
    }

    /* The file layer works in a directory, the way the SD card does. */
    slash = strrchr(argv[1], '/');
    if (slash) {
        size_t len = (size_t)(slash - argv[1]);
        memcpy(dir, argv[1], len);
        dir[len] = '\0';
        snprintf(base, sizeof base, "%s", slash + 1);
    } else {
        strcpy(dir, ".");
        snprintf(base, sizeof base, "%s", argv[1]);
    }

    /* 2 MB, the largest a Commander X16 can have. Anything that needs more
     * than this would not import on real hardware either. */
    if (!bankmem_host_init(255)) {
        fprintf(stderr, "cannot allocate simulated banked RAM\n");
        return 1;
    }
    bank_heap_init(1, 0);
    wb_init();
    file_host_set_root(dir);

    printf("=== %s ===\n", argv[1]);

    /* --- the archive ------------------------------------------------ */
    err = zip_open(&z, base);
    if (err != ERR_OK) {
        printf("  cannot open as a ZIP archive: %s\n", err_message(err));
        return 1;
    }
    printf("\nentries (%u):\n", z.entry_count);
    for (err = zip_first(&z, &e); err == ERR_OK; err = zip_next(&z, &e)) {
        printf("  %-42s %8lu -> %8lu  %s\n", e.name,
               (unsigned long)e.comp_size, (unsigned long)e.uncomp_size,
               e.method == ZIP_DEFLATE ? "deflate" :
               e.method == ZIP_STORED  ? "stored"  : "OTHER");
        if (e.method != ZIP_DEFLATE && e.method != ZIP_STORED)
            ++n;
    }
    zip_close(&z);
    if (n)
        printf("  !! %u entries use a compression method we do not read\n", n);

    /* --- the workbook ------------------------------------------------ */
    err = fetch(base, "xl/workbook.xml", &expanded);
    if (err != ERR_OK) {
        printf("\nxl/workbook.xml: %s\n", err_message(err));
        return 1;
    }
    printf("\nworkbook.xml expands to %lu bytes\n",
           (unsigned long)expanded.len);

    err = xlsx_parse_workbook(&expanded, &bk);
    if (err != ERR_OK) {
        printf("  parse failed: %s\n", err_message(err));
        return 1;
    }
    printf("sheets (%u%s):\n", bk.sheet_count,
           bk.sheets_dropped ? ", MORE DROPPED" : "");
    for (i = 0; i < bk.sheet_count; ++i)
        printf("  %-32s rId=%-8s %s\n", bk.sheet[i].name, bk.sheet[i].rid,
               bk.sheet[i].hidden ? "(hidden)" : "");

    /* --- relationships ------------------------------------------------ */
    err = fetch(base, "xl/_rels/workbook.xml.rels", &expanded);
    if (err == ERR_OK) {
        xlsx_parse_rels(&expanded, &bk);
        printf("\nsheet parts:\n");
        for (i = 0; i < bk.sheet_count; ++i) {
            const char *p = bk.sheet[i].path;
            err_t f = p[0] ? fetch(base, p, &expanded) : ERR_NOTFOUND;
            printf("  %-32s %-30s %s", bk.sheet[i].name,
                   p[0] ? p : "(no relationship)",
                   f == ERR_OK ? "ok" : err_message(f));
            if (f == ERR_OK)
                printf("  %lu bytes", (unsigned long)expanded.len);
            printf("\n");
        }
    } else {
        printf("\nxl/_rels/workbook.xml.rels: %s\n", err_message(err));
    }

    /* --- shared strings ------------------------------------------------ */
    err = fetch(base, "xl/sharedStrings.xml", &expanded);
    if (err == ERR_NOTFOUND) {
        printf("\nno sharedStrings.xml (a workbook with no text has none)\n");
    } else if (err != ERR_OK) {
        printf("\nsharedStrings.xml: %s\n", err_message(err));
    } else {
        err = xlsx_parse_strings(&expanded, &bk, ids, X16S_MAX_STRINGS);
        printf("\nshared strings: %u%s\n", bk.string_count,
               err == ERR_LIMIT ? "  (MORE THAN WE HOLD)" : "");
        for (i = 0; i < bk.string_count && i < 12; ++i) {
            strpool_get(ids[i], buf, sizeof buf);
            printf("  [%3u] \"%s\"\n", i, buf);
        }
        if (bk.string_count > 12)
            printf("  ... %u more\n", bk.string_count - 12);
    }

    if (bk.lossy)
        printf("\n!! some text had no ISO-8859-15 equivalent and became '?'\n");

    /* --- what a real import would face next --------------------------- */
    err = fetch(base, "xl/styles.xml", &expanded);
    printf("\nstyles.xml: %s", err == ERR_OK ? "present" : err_message(err));
    if (err == ERR_OK)
        printf(", %lu bytes", (unsigned long)expanded.len);
    printf("\n");

    {
        bank_stats_t st;
        bank_stats(&st);
        printf("\nbanked RAM: %lu of %lu bytes used at the end\n",
               (unsigned long)st.used, (unsigned long)st.total);
    }

    bankmem_host_free();
    return 0;
}
