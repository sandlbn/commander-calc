/* xlsx.h — reading the parts of an .xlsx package.
 *
 * Each function parses one already-decompressed part out of a blob. They
 * live in OVL_XLSX with the XML tokenizer; the driver that fetches and
 * expands the parts is resident, because only resident code can swap
 * overlays.
 *
 * Nothing validates. An .xlsx contains a great deal this program will never
 * model, and the rule throughout is that unrecognised content is stepped
 * over rather than treated as an error — a workbook with a chart in it must
 * still open.
 *
 * ONE SHEET AT A TIME. An earlier shape held all sixteen sheets in one
 * struct, which is 1488 bytes: more than the whole resident budget, and it
 * has to survive the overlay swaps between phases. Since the importer takes
 * one sheet and reports the others as skipped, describing one sheet at a
 * time costs 98 bytes and needs no banked staging at all. When multiple
 * sheets land, the driver runs this once per sheet rather than growing the
 * struct.
 */
#ifndef X16S_XLSX_H
#define X16S_XLSX_H

#include "blob.h"

#define XLSX_RID_MAX       8    /* "rId1" .. "rId9999"                   */
#define XLSX_PATH_MAX     32    /* "xl/worksheets/sheet1.xml" is 24      */

typedef struct {
    char     name[X16S_MAX_SHEET_NAME + 1];
    /* The relationship id is what the path is looked up by, and it is dead
     * the moment the lookup succeeds — xlsx_find_path reads `rid` and
     * writes `path`, in that order, and nothing reads `rid` again. Sharing
     * the bytes costs nothing and this struct is resident. */
    union {
        char rid[XLSX_RID_MAX];
        char path[XLSX_PATH_MAX];   /* filled in from the relationships */
    } u;
    uint8_t  sheet_count;           /* how many the workbook declares   */
    uint8_t  index;                 /* which one this describes         */
    uint8_t  hidden;
    uint8_t  lossy;                 /* text that would not fit the charset */
} xlsx_target_t;

/* Note what is NOT here: the shared-string count. It describes the workbook,
 * not a sheet, and this struct is reset for each sheet the driver visits —
 * so a workbook-level value stored here would survive exactly one sheet and
 * silently read as zero afterwards, which resolves every shared string to
 * nothing. It is returned separately instead. */

/* Parse xl/workbook.xml and describe sheet number `n`, counting all of them.
 * ERR_NOTFOUND if the workbook has fewer than n+1 sheets. */
err_t xlsx_find_sheet(blob_t *xml, uint8_t n, xlsx_target_t *t);

/* Parse xl/_rels/workbook.xml.rels and fill in `t->u.path` from `t->u.rid`.
 * ERR_NOTFOUND if no relationship names it — which the driver reports and
 * carries on from, rather than failing the whole import. */
err_t xlsx_find_path(blob_t *xml, xlsx_target_t *t);

/* Parse xl/sharedStrings.xml into the workbook's string pool, in order.
 *
 * Rich text is concatenated: <si><r><t>Hello</t></r><r><t> world</t></r></si>
 * imports as "Hello world". Per-run formatting is dropped.
 *
 * `ids` is a banked block of uint16 pool ids, indexed the way cells refer to
 * them. Banked rather than a plain array because a workbook may declare
 * thousands of strings, and 8192 of them is 16 KB — far past anything main
 * RAM can hold. */
err_t xlsx_parse_strings(blob_t *xml, handle_t ids, uint16_t max_ids,
                         uint16_t *count, uint8_t *lossy);

#endif /* X16S_XLSX_H */
