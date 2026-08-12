/* xlsx_ctx.h — the state an .xlsx import carries between overlay swaps.
 *
 * Importing a workbook is ten overlay loads across five overlays, and only
 * resident code can swap an overlay: an overlay that loaded another would
 * unload itself mid-call. The obvious shape — one resident function that
 * calls each phase in turn — put the whole sequence in resident memory,
 * about 1.55 KB of it, which is more than the program has to spare.
 *
 * So only the swap is resident. The driver is a loop that loads an overlay
 * and calls its step function; each step does one part of the import and
 * returns the overlay to load next, or 0 when there is nothing left. The
 * logic moves into the overlays that already hold the matching parser,
 * where it is free — an overlay's code costs nothing resident. What stays
 * resident is this struct, because it has to survive the swaps.
 *
 * Everything large is a handle rather than a buffer, for the same reason:
 * the shared-string map may be thousands of entries and the style table is
 * 512 bytes, but in banked RAM they are four bytes each here.
 */
#ifndef X16S_XLSX_CTX_H
#define X16S_XLSX_CTX_H

#include "../formula/formula.h"
#include "blob.h"
#include "xlsx.h"
#include "xlsx_sheet.h"
#include "xlsx_import.h"

/* Which part of the package the driver is on. The order matters: the steps
 * advance through it by incrementing, and the relationships have to be read
 * before the worksheet's path is known. */
#define XP_WORKBOOK 0
#define XP_RELS     1
#define XP_STRINGS  2
#define XP_STYLES   3
#define XP_SHEET    4

/* Room for 1024 shared strings. Beyond that the extras resolve to nothing
 * and the import reports itself truncated, which is the documented
 * behaviour at a limit — a workbook with more unique strings than this is
 * past what the cell store could hold anyway. */
#define XLSX_ID_MAX 1024

/* Formulas found in the worksheet, waiting to be compiled.
 *
 * They cannot be compiled where they are found: the sheet parser is in
 * OVL_SHEET and the compiler in OVL_FCOMPILE, and an overlay that loaded
 * another would unload itself mid-call. So the parser records where each
 * formula was and what it said, and the driver compiles the lot afterwards
 * with the compiler loaded once — rather than through wb_set_text(), which
 * loads the compiler and then the evaluator for every single formula.
 *
 * Six bytes an entry: sheet, row, column, and the pool id of the source
 * text. The sheet is in there because the whole batch is compiled after the
 * import, by which time the active sheet is whichever one came last. */
#define XLSX_FORMULA_MAX FPEND_MAX
/* The pending-list layout lives in formula.h now: the .X16S reader
 * collects formulas the same way. These names are kept because the
 * importer is written in terms of them. */
#define XLSX_FFIELD      FPEND_FIELD

typedef struct {
    const char        *archive;
    xlsx_report_t      rep;         /* the caller reads it via xlsx_result */
    blob_t             staged;      /* the part as it came out of the ZIP */
    blob_t             part;        /* expanded, ready to parse           */
    xlsx_target_t      target;      /* one sheet at a time: see xlsx.h    */
    xlsx_styles_info_t sinfo;
    handle_t           ids;         /* shared-string index -> pool id     */
    /* The formulas awaiting compilation are in formula_pending now: the
     * .X16S reader collects them the same way and the list is not an
     * xlsx thing any more. See formula.h. */
    handle_t           styles;      /* style index -> number format       */
    uint32_t           raw_size;    /* what `staged` expands to           */
    uint8_t            part_no;     /* XP_*                               */
    uint8_t            sheet_no;    /* which worksheet is being read      */
    err_t              err;
} xlsx_ctx_t;

extern xlsx_ctx_t xctx;

/* Each of these runs inside the overlay its name refers to, does one part of
 * the import, and returns the overlay the driver should load next — or 0
 * when the import is over, whether it succeeded or failed. Failure is
 * reported by setting xctx.err before returning 0, so the driver needs to
 * know nothing about what any given step can go wrong at. */
/* Note a formula the worksheet parser found, for compiling once the import
 * is over. Resident, because the parser's overlay is full: OVL_SHEET holds
 * the XML tokenizer and had 198 bytes spare. */
void xlsx_note_formula(uint16_t row, uint16_t col, const char *src,
                       uint16_t len);

/* Create or rename the worksheet the driver is about to fill, and make it
 * the active one. Needs OVL_MENU, which is where wb_sheet_add() lives. */
err_t xlsx_open_sheet(void);

uint8_t xlsx_step_zip(void);
uint8_t xlsx_step_xml(void);
uint8_t xlsx_step_style(void);
uint8_t xlsx_step_sheet(void);

/* Which overlay parses part `n`. Both the steps that hand off to a parser
 * need this, and a copy inside each of their overlays costs nothing. */
#define XLSX_PARSER(n) ((n) == XP_STYLES ? OVL_STYLE :  \
                        (n) == XP_SHEET  ? OVL_SHEET :  \
                                           OVL_XLSX)

#endif /* X16S_XLSX_CTX_H */
