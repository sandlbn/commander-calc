/* xlsx_export.h — writing the workbook as an .xlsx.
 *
 * The mirror of xlsx_import.h, and much simpler than it, for one reason:
 * reading an .xlsx needs the ZIP reader, the inflate decoder, the XML
 * tokenizer and three parsers, which live in five different overlays and
 * have to be sequenced by resident code. Writing one needs a ZIP writer and
 * some string building, which fit in a single overlay together. So this is
 * one call rather than a state machine, and costs the resident core the
 * ovl_require() in front of it and nothing else.
 *
 * What is written:
 *
 *   [Content_Types].xml          what each part is
 *   _rels/.rels                  the package points at the workbook
 *   xl/workbook.xml              one sheet, named as it is here
 *   xl/_rels/workbook.xml.rels   the workbook points at the sheet
 *   xl/styles.xml                one xf per style, so formats survive
 *   xl/worksheets/sheet1.xml     the cells
 *
 * Text goes inline, in <is><t>, rather than into a shared string table.
 * That drops a whole part and the pass that would build it, and costs
 * repetition in a file nobody reads by hand — the same trade the stored
 * ZIP entries make. Excel, LibreOffice and Google Sheets all read it, and
 * so does our own importer.
 */
#ifndef X16S_XLSX_EXPORT_H
#define X16S_XLSX_EXPORT_H

#include "../util/errors.h"
#include "../x16sheet.h"

/* Write the current workbook to `name`, replacing it if it exists.
 *
 * The file is written in one pass and is only valid once the central
 * directory lands at the end, so a failure part-way leaves a file that is
 * not an .xlsx rather than one that opens with content missing. */
err_t xlsx_export(const char *name);

/* The three halves of it, each in the overlay that holds its code. Only
 * xlsx_export() above should call these, and only in this order.
 *
 * The archive state lives in banked RAM between them -- see the note in
 * xlsx_export.c -- because neither overlay survives the other being
 * loaded. `xw_state` is the four resident bytes that finds it again. */
extern handle_t xw_state;

err_t xlsx_export_begin(const char *name);   /* OVL_XLSX_OUT1 */
err_t xlsx_export_body(void);                /* OVL_XLSX_OUT2 */
err_t xlsx_export_finish(uint8_t ok);        /* OVL_XLSX_OUT1 */

#endif /* X16S_XLSX_EXPORT_H */
