/* xlsx_import.h — the resident driver that opens an .xlsx.
 *
 * Resident because only resident code can swap overlays, and importing a
 * workbook is five phases in four different ones:
 *
 *   OVL_ZIP      stage a part out of the archive
 *   OVL_INFLATE  expand it
 *   OVL_XLSX     read workbook.xml, the relationships, the shared strings
 *   OVL_STYLE    read the number formats
 *   OVL_SHEET    read the cells
 *
 * Every part goes through the first two, so the sequence is long but each
 * step is small. What keeps this affordable in resident memory is that
 * nothing large crosses a phase: one sheet is described at a time (98
 * bytes), and the shared-string map lives in banked RAM.
 */
#ifndef X16S_XLSX_IMPORT_H
#define X16S_XLSX_IMPORT_H

#include "../util/errors.h"

typedef struct xlsx_report_s {
    uint8_t  sheets_found;      /* how many the workbook declares       */
    uint8_t  sheets_skipped;    /* all but the one imported             */
    uint16_t cells;
    uint16_t formulas;          /* cells that carried one               */
    /* Of those, how many this program could translate. The rest keep the
     * value Excel cached, which is why an unsupported formula still shows
     * the right answer -- it just will not change when its inputs do. */
    uint16_t formulas_live;
    uint16_t dates;
    uint16_t strings;
    uint8_t  formats;           /* number formats converted             */
    uint8_t  truncated;         /* something did not fit                */
    uint8_t  lossy;             /* text changed to fit the charset      */
    /* No sheet name: every sheet is imported now, so there is no one sheet
     * the report is about. Anything wanting a title asks the workbook --
     * wb_tab_name(0, ...) is resident and is the truth. */
} xlsx_report_t;

/* Replace the workbook with the first sheet of `archive`. */
err_t xlsx_import(const char *archive);

/* What the last import managed. Filled in whether it succeeded or failed, so
 * the caller can tell the user what did get through — unsupported content is
 * skipped and counted rather than treated as an error, because a workbook
 * with a chart in it must still open.
 *
 * Returned by pointer rather than by value, and owned by the importer rather
 * than the caller: a copy on the caller's stack costs main RAM at both ends,
 * and there is only ever one import to report on. */
const xlsx_report_t *xlsx_result(void);

#endif /* X16S_XLSX_IMPORT_H */
